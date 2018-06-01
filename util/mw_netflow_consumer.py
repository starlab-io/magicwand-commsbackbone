#!/usr/bin/env python2

MW_XENSTORE_ROOT = b"/mw"
MW_XENSTORE_NETFLOW = MW_XENSTORE_ROOT + b"/pvm/netflow"

import sys
import os
import random
import signal
import socket
import pyxs
import struct
import select
import tty
import termios
import threading

SIG_NF_INFO = 0xd310
SIG_FEA_REQ = 0xd320
SIG_FEA_RES = 0xd32f

FEA_FLAG_WRITE   = 0x1
FEA_FLAG_BY_SOCK = 0x2

FEATURES = dict(
    MtSockAttribNone          = (0, bool),

    # *** Non-socket related commands

    # Turn on traffic monitoring for channel
    MtChannelTrafficMonitorOn     = (0x0001, None),

    # Turn off traffic monitoring for channel
    MtChannelTrafficMonitorOff    = (0x0002, None),

    # *** Per-socket features: name => (value, argtype)

    MtSockAttribOwnerRunning  = (0x0101, bool), # arg: bool
    MtSockAttribOpen          = (0x0102, bool), # arg: bool

    MtSockAttribSndBuf       = (0x0109, int), # arg: sz in bytes (uint32_t)
    MtSockAttribRcvBuf       = (0x010a, int), # arg: sz in bytes (uint32_t)

    MtSockAttribSndTimeo     = (0x010b, tuple), # arg: (s,us) tuple
    MtSockAttribRcvTimeo     = (0x010c, tuple), # arg: (s,us) tuple

    MtSockAttribSndLoWat     = (0x010d, int), # arg: sz in bytes (uint32_t)
    MtSockAttribRcvLoWat     = (0x010e, int), # arg: sz in bytes (uint32_t)

    # *** INS-wide changes

    # Change congestion control algo, arg: see vals below
    MtSockAttribSystemInsCongctl    = (0x20, str), # arg: mw_congctl_arg_val_t (uint32_t)

    # Change of delay ACK ticks: arg is signed tick differential
    MtSockAttribSystemDelackTicks   = (0x21, int),  # arg: tick count (uint32_t)
)

FLAGS = dict(
    MW_FEATURE_FLAG_WRITE   = 0x1,
    MW_FEATURE_FLAG_BY_SOCK = 0x2,
)

CONGCTL_ALGO = dict(
    MwCongctlReno    = 0x90, # action: congctl
    MwCongctlNewReno = 0x91, # action: congctl
    MwCongctlCubic   = 0x92, # action: congctl
)

# mw_base_t: the beginning of every message
BASE_FMT = "!HI"

# mw_addr_t, not standalone (no byte ordering given)
# addr is transmitted in network byte ordering as stored in sockaddr*
ADDR_FMT = "I" + "16s" # 20 bytes

# mw_endpoint_t, not standalone
ENDPOINT_FMT = ADDR_FMT + "H"

##
## Input read from netflow channel
##

# mw_netflow_info_t, post-signature
INFO_FMT = "!HQQQQQ" + ENDPOINT_FMT + ENDPOINT_FMT + "QQQ"

# Feature response, post-signature
FEATURE_RES_FMT = "!i16s"

##
## Output written to netflow channel
##

# Feature request by sockfd, last substring is sockfd + rest of addr
FEATURE_REQ_FMT = BASE_FMT + "HH" + "QQ" + "i16s"

BASE_FMT_PART = "!I" # base is sig + id
BASE_FMT_ALL  = "!HI"

ADDR_FMT = "!HH16s"

MAX_ID = 0xffffffff

outstanding_requests = dict()
open_socks = dict() # sockfd => True

info_display = True
info_muted = False
prg_run = True
curr_id = random.randint( 0, MAX_ID )


class SignalHandler( object ):
    def setup( self, sock, term_attr, thread_p ):        
        self.sock = sock
        self.term_attr = term_attr
        self.thread_p = thread_p
        signal.signal( signal.SIGINT, self.catch )

    def catch( self, signum, frame ):
        cleanup( self.sock, self.term_attr, self.thread_p )


def cleanup( sock, term_attr, thread_p ):
    print('\n*** Cleanup and exit ***')

    global prg_run
    termios.tcsetattr(sys.stdin, termios.TCSADRAIN, term_attr)
    prg_run = False
    thread_p.join()
    sock.close
    sys.exit(0)

def isData():
    return select.select([sys.stdin], [], [], 0) == ([sys.stdin], [], [])


def b2h( bytes ):
    """ Convert byte string to hex ASCII string """
    return ' '.join( [ '{0:02x}'.format( ord(y) ) for y in bytes ] )


def recvall( sock, size ):
    tot = 0
    data = b""
    while tot < size:
        d = sock.recv( size - tot )
        if not len(d):
            break # nothing read, we won't read all the requested data
        tot += len(d)
        data += d

    return data


def get_netflow_info():
    try:
        with pyxs.Client() as c:
            return c[ MW_XENSTORE_NETFLOW ]
    except:
        print("XenStore path: {}".format( MW_XENSTORE_NETFLOW ))
        raise


def connect_to_netflow( server ):
    print( "Connecting to {0}\n".format( server ) )

    if server.find("[", 0, 2) == -1:
        # IPv4
        ip, port = server.split(':')
    else:
        # IPv6
        new_ip, port = server.rsplit(':', 1)
        print new_ip
        ip = new_ip.strip('[]')

    sock = socket.socket( socket.AF_INET, socket.SOCK_STREAM )

    try:
        sock.connect( (ip, int(port, 10)) )
        return sock
    except:
        print( "connect: sock = {}, ip = {}, port = {}".format( sock, ip, port) )
        raise


class Endpoint:
    def __init__( self, af, ip, port ):
        self._af   = af
        self._port = port

        if 4 == af:
            self._ip = ip[:4]
        elif 6 == af:
            self._ip = ip
        else:
            import pdb;pdb.set_trace()
            raise RuntimeError( "Invalid IP address family" )

    def __str__ ( self ):
        if 4 == self._af:
            i = socket.inet_ntop( socket.AF_INET, self._ip )
        elif 6 == self._af:
            i = '[' + socket.inet_ntop( socket.AF_INET6, self._ip ) + ']'

        return "{0}:{1}".format( i, self._port )


def get_obs_str( obs ):
    return { 0 : "none",
             1 : "create",
             2 : "bind",
             3 : "accept",
             4 : "connect",
             5 : "recv",
             6 : "send",
             7 : "close" }.get( obs, "invalid" )


def process_info_netflow( sock ):
    try:
        raw = recvall( sock, struct.calcsize( INFO_FMT ) )
        vals = struct.unpack( INFO_FMT, raw )
    except Exception, e:
        raise

    ( o, start_sec, start_ns, curr_sec, curr_ns ) = vals[:5]
    ( sockfd ) = vals[5]
    ( pf, pa, pp ) = vals[6:9]
    ( rf, ra, rp ) = vals[9:12]
    ( inbytes, outbytes, extra ) = vals[12:]

    start_time = start_sec + start_ns / 1000000000.
    event_time = curr_sec  + curr_ns  / 1000000000.
    age = event_time - start_time

    remote = Endpoint( rf, ra, rp )
    pvm = Endpoint( pf, pa, pp )
    obs = get_obs_str( o )

    if not info_muted:
        print( "[{0:0.3f}] {1:x} {2} | {3} {4} bytes {5}/{6} ext {7:x}".
               format( age, sockfd, pvm, remote, 
               obs, inbytes, outbytes, extra ) )

    if obs is "create":
        print("Adding sockfd {} to open_socks".format(sockfd))
        open_socks[ sockfd ] = True
    elif obs is "accept":
        #assert sockfd in open_socks
        # TODO: need to add the new sockfd created by accept
        print("accept, sockfd {0:x} extra {1:x}".format(sockfd, extra))
        open_socks[ extra ] = True
    elif obs is "close":
        if sockfd in open_socks:
            print("Removing sockfd {0:x} from open_socks".format(sockfd))
            del open_socks[ sockfd ]
        else:
            print("Removing sockfd {0:x} from open_socks, but does not exist".format(sockfd))


def get_next_id():
    global curr_id
    curr_id += 1

    while curr_id in outstanding_requests:
        curr_id += 1
        curr_id %= MAX_ID

    outstanding_requests[ curr_id ] = None

    return curr_id


def send_feature_request( conn, sockfd, name, value=None ):
    """
    value is 2-tuple. Once the request is sent, the PVM must send us a
    response. However, we can't enforce that on this side.
    """
    ident = get_next_id()

    modify = (None == value)
    val = value
    if not val:
        val = ( 0, 0 )

    outstanding_requests[ ident ] = { 'type'    : 'feature',
                                      'mod'     : modify,
                                      'sockfd'  : sockfd,
                                      'name'    : name,
                                      'value'   : value }

    flag = 'MW_FEATURE_FLAG_BY_SOCK'

    req = struct.pack( FEATURE_REQ_FMT, # everything, including signature
                       SIG_FEA_REQ, ident,
                       FLAGS[ flag ], name,
                       val[0], val[1],
                       sockfd, "" )

    print( "Feature request\tid {0:x}: {1:x} name {2:x} flags {3:x} [{4:x}:{5:x}]"
           .format( ident, sockfd, name, FLAGS[ flag ], val[0], val[1] ) )

    conn.sendall( req )


def send_feature_request2( conn ):
    """ Exercises feature request/response system with randomized requests """
    ident = get_next_id()
    modify = random.randint(0,1)

    flags  = FLAGS['MW_FEATURE_FLAG_BY_SOCK']
    feature = random.choice( FEATURES.items() )
    #modify = 1##random.randint(0,1)
    #feature = ( 'MtSockAttribOpen', (0x0102, bool) )

    name = feature[1][0]
    sockfd = random.choice( open_socks.keys() )

    val = (0,0)
    if modify:
        flags |= FEA_FLAG_WRITE
        if type(False) == feature[1][1]:
            val = ( random.randint(0,1), 0 )
        elif type(5) == feature[1][1]:
            val = ( random.randint(0,0x10000), 0 )
        elif type(()) == feature[1][1]:
            val = ( random.randint(0,0x10000), random.randint(0,0x10000) )
        else:
            print( "Bad type {0}".type(features[1][1]) )
            import pdb;pdb.set_trace()
            return

    req = struct.pack( FEATURE_REQ_FMT, # everything, including signature
                       SIG_FEA_REQ, ident, # header
                       flags, name, val[0], val[1], sockfd, "" )

    print( "Feature request id {0:x}: {1:x} name {2:x} flags {3:x} [{4:x}:{5:x}]"
           .format( ident, sockfd, name, flags, val[0], val[1] ) )

    conn.sendall( req )


def process_feature_response( sock, ident ):
    data = recvall( sock, struct.calcsize( FEATURE_RES_FMT ) )
    status, val = struct.unpack( FEATURE_RES_FMT, data )

    state = outstanding_requests[ ident ]
    del outstanding_requests[ ident ]

    vals = struct.unpack( "!QQ", val )

    # lookup ident for more info
    print( "Feature response id {0:x}: {1:x} [{2:x}:{3:x}]"
           .format( ident, status, vals[0], vals[1] ) )


def netflow_monitor( conn ):
    global prg_run

    while prg_run:

        # Check for messages
        sig, ident = struct.unpack( BASE_FMT, conn.recv( struct.calcsize( BASE_FMT ) ) )
        if SIG_NF_INFO == sig:
            process_info_netflow( conn )
        elif SIG_FEA_RES == sig:
            process_feature_response( conn, ident )
        else:
            print("Unknown signature: {}".format ( sig ) )


def input_monitor( conn ):
    global info_display
    global info_muted
    global prg_run

    while prg_run:

        # Check for user input
        if isData():
            c = sys.stdin.read(1)
            if c == '\x71':      # 'q'
                print("*** GoodBye ***")
                prg_run = False
                # In case parent is blocked on recv()
                os.kill(os.getpid(), signal.SIGINT)
            elif c == '\x68':    # 'h'
                print("*** Commands ***")
                print("q - quit")
                print("h - help")
                print("m - netflow information monitor un-muted")
                print("M - netflow information monitor muted")
                print("o - netflow information monitor on (enables open socket list")
                print("O - netflow information monitor off (disables open socket list)")
                print("p - print open sockets")
            elif c == '\x6d':    # 'm'
                info_muted = False
                print("*** NetFlow Information Display (Un-Muted) ***")
            elif c == '\x4d':    # 'M'
                info_muted = True
                print("*** NetFlow Information Display (Muted) ***")
            elif c == '\x6f':    # 'o'
                send_feature_request( conn, 0, FEATURES[ 'MtChannelTrafficMonitorOn' ][0], None )
                info_display = True
                print("*** NetFlow Information Display (On) ***")
            elif c == '\x4f':    # 'O'
                send_feature_request( conn, 0, FEATURES[ 'MtChannelTrafficMonitorOff' ][0], None )
                info_display = False
                open_socks.clear()
                print("*** NetFlow Information Display (Off) ***")
            elif c == '\x70':    # 'p'
                if info_display:
                    print("*** Open socket list ***")
                    for sock in open_socks.keys():
                        print("socket = {0:x}".format(sock))
                else:
                    print("*** Open socket list (disabled when traffic monitor is off) ***")


if __name__ == '__main__':
    if os.geteuid() != 0:
        print("You need to have root privileges to run this script")
        sys.exit(1)

    server = get_netflow_info()

    sock = connect_to_netflow( server )

    term_attr = termios.tcgetattr(sys.stdin)
    tty.setcbreak(sys.stdin.fileno())

    thread_p = threading.Thread(name='input monitor', target=input_monitor, args=(sock,))
    thread_p.start()

    sig_handler = SignalHandler()
    sig_handler.setup( sock, term_attr, thread_p )

    netflow_monitor( sock )

    cleanup( sock, term_attr, thread_p )

