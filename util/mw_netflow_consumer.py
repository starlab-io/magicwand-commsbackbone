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
SIG_FEA_RES = 0xd321

#SIG_MIT_REQ = 0xd320
#SIG_MIT_RES = 0xd321
#SIG_STA_REQ = 0xd330
#SIG_STA_RES = 0xd331

FEATURES = dict(
    MwFeatureNone = 0x00,

    # Per-socket features
    MwFeatureOwningThreadRunning = (0x01, bool), # arg: bool
    MwFeatureSocketOpen          = (0x02, bool), # arg: bool

    MwFeatureSocketSendBuf       = (0x10, int), # arg: sz in bytes (uint32_t)
    MwFeatureSocketRecvBuf       = (0x11, int), # arg: sz in bytes (uint32_t)
    MwFeatureSocketSendLoWat     = (0x12, int), # arg: sz in bytes (uint32_t)
    MwFeatureSocketRecvLoWat     = (0x13, int), # arg: sz in bytes (uint32_t)
    MwFeatureSocketSendTimo      = (0x14, int), # arg: milliseconds (uint64_t)
    MwFeatureSocketRecvTimo      = (0x15, int), # arg: milliseconds (uint64_t)

    # INS-wide changes

    # Change congestion control algo, arg: see vals below
    MwFeatureSystemInsCongctl    = (0x20, str), # arg: mw_congctl_arg_val_t (uint32_t)

    # Change of delay ACK ticks: arg is signed tick differential
    MwFeatureSystemDelackTicks   = (0x21, int),  # arg: tick count (uint32_t)
)

CONGCTL_ALGO = dict(
    MwCongctlReno    = 0x90, # action: congctl
    MwCongctlNewReno = 0x91, # action: congctl
    MwCongctlCubic   = 0x92, # action: congctl
)

SIG_FMT       = "!H"
BASE_FMT_PART = "!I" # base is sig + id
BASE_FMT_ALL  = "!HI"


ADDR_FMT = "!HH16s"
INFO_FMT = "!HQQQQi20s20sQQQ" # everything after the signature

# All of feature request
FEATURE_REQ_FMT = "!HIHHiQ"

# Feature response, after the signature
FEATURE_RES_FMT = "!IiQ"

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


class Ip:
    def __init__( self, packed ):

        (ver, port, ip) = struct.unpack( ADDR_FMT, packed )

        self._ver = ver
        self._port = port

        if 4 == ver:
            self._ip = ip[:4]
        elif 6 == ver:
            self._ip = ip
        else:
            raise RuntimeError( "Invalid IP address family" )

    def __str__ ( self ):
        if 4 == self._ver:
            i = socket.inet_ntop( socket.AF_INET, self._ip )
        elif 6 == self._ver:
            i = '[' + socket.inet_ntop( socket.AF_INET6, self._ip ) + ']'

        return "{0}:{1}".format( i, self._port )

def get_obs_str( obs ):
    return { 0 : "none",
             1 : "bind",
             2 : "accept",
             3 : "connect",
             4 : "recv",
             5 : "send",
             6 : "close" }.get( obs, "invalid" )

def process_info_netflow( sock ):
    try:
        raw = recvall( sock, struct.calcsize( INFO_FMT ) )
        vals = struct.unpack( INFO_FMT, raw )

    except Exception, e:
        import pdb;pdb.set_trace()
        raise

    ( o, start_sec, start_ns, curr_sec, curr_ns ) = vals[:5]
    ( sockfd, p, r, inbytes, outbytes, extra )    = vals[5:]

    start_time = start_sec + start_ns / 1000000000.
    event_time = curr_sec  + curr_ns  / 1000000000.
    age = event_time - start_time

    remote = Ip( r )
    pvm    = Ip( p )
    obs    = get_obs_str( o )

    if obs is "bind":
        open_socks[ sockfd ] = True
    elif obs is "accept":
        assert sockfd in open_socks
        open_socks[ extra ] = True
    elif obs is "close":
        del open_socks[ sockfd ]

    print( "[{0:0.3f}] {1:x} {2} | {3} {4} bytes {5}/{6} ext {7:x}".
           format( age, sockfd, pvm, remote, 
                   obs, inbytes, outbytes, extra ) )

def get_next_id():
    curr_id = random.randint( 0, MAX_ID )

    while curr_id in outstanding_requests:
        curr_id = random.randint( 0, MAX_ID )

    outstanding_requests[ curr_id ] = None

    return curr_id

def send_feature_request( conn, sockfd, feature, value=None ):
    ident = get_next_id()

    modify = (None == value)
    val = value
    if not val:
        val = 0

    outstanding_requests[ ident ] = { 'type'    : 'feature',
                                      'mod'     : modify,
                                      'sockfd'  : sockfd,
                                      'feature' : feature,
                                      'value'   : value }

    req = struct.pack( FEATURE_REQ_FMT, # everything, including signature
                       SIG_FEA_REQ, ident, int(modify), feature, sockfd, val )

    logging.info( "Sending {0}".format( b2h( req ) ) )
    conn.sendall( req )

def process_feature_response( sock ):
    data = recvall( sock, struct.calcsize( FEATURE_RES_FMT ) )
    ident, status, val = struct.unpack( FEATURE_RES_FMT, data )

    state = outstanding_requests[ ident ]
    del outstanding_requests[ ident ]

    # lookup ident for more info
    print( "Feature response\tid {0:x}: {1:x} [{2:x}]".format( ident, status, val ) )

def monitor_netflow( conn ):
    iters = 0
    while True:
        iters += 1
        sig, = struct.unpack( SIG_FMT, conn.recv( struct.calcsize( SIG_FMT ) ) )
        if SIG_NF_INFO == sig:
            process_info_netflow( conn )

            # Exercise feature requests/mitigation given a valid socket
            # For now, PVM just reads requests and writes responses;
            # no processing is done
            if iters % 2 == 0 and len(open_socks) > 0:
                sockfd = open_socks.keys()[0] # grab sockfd "at random"
                send_feature_request( conn, sockfd, FEATURES[ 'MwFeatureSocketSendBuf' ][0], None ) 

        elif SIG_FEA_RES == sig:
            process_feature_response( conn )
        else:
            continue


if __name__ == '__main__':
    logging.basicConfig( level=logging.DEBUG )

    server = get_netflow_info()

    s = connect_to_netflow( server )

    monitor_netflow( s )
