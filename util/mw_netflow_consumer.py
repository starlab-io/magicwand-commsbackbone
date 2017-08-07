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

FEATURE_REQ_FMT = "!HIHHiQ" # everything, including signature
FEATURE_RES_FMT = "!IIQ"   # everything after the signature

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

    print( "[{0:0.3f}] {1:x} {2} | {3} {4} bytes {5}/{6} ext {7:x}".
           format( age, sockfd, pvm, remote, 
                   obs, inbytes, outbytes, extra ) )

outstanding_requests = dict()
MAX_ID = 0xffffffff
def get_next_id():
    curr_id = random.randint( 0, MAX_ID )

    while curr_id in outstanding_requests:
        curr_id = random.randint( 0, MAX_ID )

    outstanding_requests[ curr_id ] = None

def send_feature_request( sock, sockfd, feature, value=None ):
    ident = get_next_id()

    modify = (None == value)
    val = value
    if not val:
        val = 0

    outstanding_requests[ ident ] = { type    : 'feature', 
                                      mod     : modify, 
                                      sockfd  : sockfd, 
                                      feature : feature,
                                      value   : value }

    req = struct.pack( FEATURE_REQ_FMT, "!HIHHiQ" # everything, including signature
                       SIG_FEA_REQ, ident, int(modify), feature, sockfd, val )
                       
    sock.sendall( req )

def process_feature_response( sock ):
    ident, status, val = recvall( sock, struct.calcsize( FEATURE_RES_FMT ) )

    state = outstanding_requests[ ident ]
    del outstanding_requests[ ident ]

    # lookup ident for more info
    logging.info( "Feature response\tid {0:x}: {1:x} [{2:r}]}".format( ident, status, val ) )

def monitor_netflow( sock ):
    while True:
        sig, = struct.unpack( SIG_FMT, sock.recv( struct.calcsize( SIG_FMT ) ) )
        if SIG_NF_INFO == sig:
            process_info_netflow( sock )
        elif SIG_FEA_RES == sig:
            process_feature_response( sock )
        else:
            continue


if __name__ == '__main__':
    logging.basicConfig( level=logging.DEBUG )

    server = get_netflow_info()

    s = connect_to_netflow( server )

    monitor_netflow( s )
