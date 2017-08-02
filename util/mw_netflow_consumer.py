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
        addr_fmt = "!HH16s"
        (ver, port, ip) = struct.unpack( addr_fmt, packed )

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
    info_fmt = "!HQQQQi20s20sQQQ"
    try:
        raw = recvall( sock, struct.calcsize( info_fmt ) )
        vals = struct.unpack( info_fmt, raw )

    except Exception, e:
        import pdb;pdb.set_trace()
        raise

    ( o, start_sec, start_ns, curr_sec, curr_ns ) = vals[:5]
    ( sockfd, p, r, inbytes, outbytes, extra )     = vals[5:]

    start_time = start_sec + start_ns / 1000000000.
    event_time = curr_sec  + curr_ns  / 1000000000.
    age = event_time - start_time

    remote = Ip( r )
    pvm    = Ip( p )
    obs    = get_obs_str( o )

    print( "[{0:0.2f}] {1:x} {2} | {3} {4} bytes {5}/{6} ext {7:x}".
           format( age, sockfd, pvm, remote, 
                   obs, inbytes, outbytes, extra ) )

def monitor_netflow( sock ):
    SIG_NF_INFO = 0xd310
    SIG_MIT_REQ = 0xd320
    SIG_MIT_RES = 0xd321
    SIG_STA_REQ = 0xd330
    SIG_STA_RES = 0xd331

    sig_fmt  = "!H"

    while True:
        sig, = struct.unpack( sig_fmt, sock.recv( struct.calcsize( sig_fmt ) ) )
        if SIG_NF_INFO == sig:
            process_info_netflow( sock )
        else:
            continue


if __name__ == '__main__':
    logging.basicConfig( level=logging.DEBUG )

    server = get_netflow_info()

    s = connect_to_netflow( server )

    monitor_netflow( s )
