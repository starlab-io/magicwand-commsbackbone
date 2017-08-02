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


def monitor_netflow( sock ):
    SIG_NF_INFO = 0xd310
    SIG_MIT_REQ = 0xd320
    SIG_MIT_RES = 0xd321
    SIG_STA_REQ = 0xd330
    SIG_STA_RES = 0xd331

    while True:
        sig, = struct.unpack( "!H", sock.recv( 2 ) )
        if SIG_NF_INFO == sig:
            info_fmt = "!HQQQQi19s19s"
            ( obs, 
              start_sec, start_ns, curr_sec, curr_ns, 
              sockfd, pvm, remote, 
              inbytes, outbytes ) = \
                        struct.unpack( info_fmt, 
                                       sock.recv( struct.calcsize( info_fmt ) ) )
            import pdb;pdb.set_trace()
        else:
            continue


if __name__ == '__main__':
    logging.basicConfig( level=logging.DEBUG )

    server = get_netflow_info()

    s = connect_to_netflow( server )

    monitor_netflow( s )
