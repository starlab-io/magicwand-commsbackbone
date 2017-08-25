#!/usr/bin/env python2

MW_XENSTORE_ROOT = b"/mw"

import sys
import os
import random
import signal
import time
import re
import httplib
import xmlrpclib
import socket
import pyxs
import logging
import struct

SIG_NF_INFO = 0xd310
SIG_FEA_REQ = 0xd320
SIG_FEA_RES = 0xd32f

FEA_FLAG_WRITE   = 0x1
FEA_FLAG_BY_SOCK = 0x2


FEATURES = dict(
    MtSockAttribNone          = (0, bool),

    # Per-socket features: name => (value, argtype)

    MtSockAttribOwnerRunning  = (0x0101, bool), # arg: bool
    MtSockAttribOpen          = (0x0102, bool), # arg: bool

    MtSockAttribSndBuf       = (0x0109, int), # arg: sz in bytes (uint32_t)
    MtSockAttribRcvBuf       = (0x010a, int), # arg: sz in bytes (uint32_t)

    MtSockAttribSndTimeo     = (0x010b, tuple), # arg: (s,us) tuple
    MtSockAttribRcvTimeo     = (0x010c, tuple), # arg: (s,us) tuple

    MtSockAttribSndLoWat     = (0x010d, int), # arg: sz in bytes (uint32_t)
    MtSockAttribRcvLoWat     = (0x010e, int), # arg: sz in bytes (uint32_t)

    # INS-wide changes

    # Change congestion control algo, arg: see vals below
    ###MtSockAttribSystemInsCongctl    = (0x20, str), # arg: mw_congctl_arg_val_t (uint32_t)

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
INFO_FMT = "!HQQQQi" + ENDPOINT_FMT + ENDPOINT_FMT + "QQQ"

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


# All of feature request


# Feature response, after the signature


MAX_ID = 0xffffffff

outstanding_requests = dict()
open_socks = dict() # sockfd => True

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
    with pyxs.Client() as c:
        return c[ MW_XENSTORE_ROOT + "/pvm/netflow" ]

def connect_to_netflow( server ):
    logging.info( "Connecting to {0}\n".format( server ) )

    if server.find("[", 0, 2) == -1:
        # IPv4
        ip, port = server.split(':')
    else:
        # IPv6
        new_ip, port = server.rsplit(':', 1)
        print new_ip
        ip = new_ip.strip('[]')

    s = socket.socket( socket.AF_INET, socket.SOCK_STREAM )

    s.connect( (ip, int(port, 10)) )

    return s


class Endpoint:
    def __init__( self, af, ip, port ):
        self._af   = af
        self._port = port

        if 4 == af:
            self._ip = ip[:4]
        elif 6 == af:
            self._ip = ip
        else:
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
    ( sockfd )     = vals[5]
    ( pf, pa, pp ) = vals[6:9]
    ( rf, ra, rp ) = vals[9:12]
    ( inbytes, outbytes, extra )    = vals[12:]

    start_time = start_sec + start_ns / 1000000000.
    event_time = curr_sec  + curr_ns  / 1000000000.
    age = event_time - start_time

    remote = Endpoint( rf, ra, rp )
    pvm    = Endpoint( pf, pa, pp )
    obs    = get_obs_str( o )

    if obs is "create":
        open_socks[ sockfd ] = True
    elif obs is "accept":
        assert sockfd in open_socks
        open_socks[ extra ] = True
    elif obs is "close":
        del open_socks[ sockfd ]

    print( "[{0:0.3f}] {1:x} {2} | {3} {4} bytes {5}/{6} ext {7:x}".
           format( age, sockfd, pvm, remote, 
                   obs, inbytes, outbytes, extra ) )

curr_id = random.randint( 0, MAX_ID )
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
            #logging.error( "Bad type {0}".type(features[1][1]) )
            import pdb;pdb.set_trace()
            return

    req = struct.pack( FEATURE_REQ_FMT, # everything, including signature
                       SIG_FEA_REQ, ident, # header
                       flags, name, val[0], val[1], sockfd, "" )

    print( "Feature request\tid {0:x}: {1:x} name {2:x} flags {3:x} [{4:x}:{5:x}]"
           .format( ident, sockfd, name, flags, val[0], val[1] ) )

    conn.sendall( req )


def process_feature_response( sock, ident ):
    data = recvall( sock, struct.calcsize( FEATURE_RES_FMT ) )
    status, val = struct.unpack( FEATURE_RES_FMT, data )

    state = outstanding_requests[ ident ]
    del outstanding_requests[ ident ]

    vals = struct.unpack( "!QQ", val )

    # lookup ident for more info
    print( "Feature response\tid {0:x}: {1:x} [{2:x}:{3:x}]"
           .format( ident, status, vals[0], vals[1] ) )


def monitor_netflow( conn ):
    iters = 0
    while True:
        iters += 1
        sig, ident = struct.unpack( BASE_FMT, conn.recv( struct.calcsize( BASE_FMT ) ) )
        if SIG_NF_INFO == sig:
            process_info_netflow( conn )

            # Exercise feature requests/mitigation given a valid socket
            # For now, PVM just reads requests and writes responses;
            # no processing is done
            if iters % 2 == 0 and len(open_socks) > 0:
                #sockfd = open_socks.keys()[0] # grab sockfd "at random"
                #send_feature_request( conn, sockfd, FEATURES[ 'MtSockAttribSndBuf' ][0], None )
                send_feature_request2( conn )
        elif SIG_FEA_RES == sig:
            process_feature_response( conn, ident )
        else:
            continue


if __name__ == '__main__':
    logging.basicConfig( level=logging.DEBUG )

    server = get_netflow_info()

    s = connect_to_netflow( server )

    monitor_netflow( s )
