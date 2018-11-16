#!/usr/bin/env python2

# Netflow Library
#
# Used to establish a communication channel with the mwcomms
# driver for monitoring protected application traffic, querying
# for information and requesting actions, such as mitigation.
# 
# Requirements:
# - run on Dom0 (requires access to XenStore) 
# - run with sufficient privileges to access XenStore 
# - pyxs module installed using pip

import sys
import os
import socket
import struct
import random
import pyxs

##
## XenStore holds connection information (IP/port)
##

MW_XENSTORE_ROOT = b"/mw"
MW_XENSTORE_NETFLOW = MW_XENSTORE_ROOT + b"/pvm/netflow"

##
## Request/Response message format
##

#
# magicwand-commsbackbone/exports/imports/mw_netflow_iface.h
#
# mw_netflow_info_t (116 bytes)
#
#   base
#     sig - netflow signature (2 bytes)
#     id - message id (4 bytes)
#
#   obs - observed syscall (2 bytes)
#
#   ts_session_start - session start time
#     sec - seconds (8 bytes)
#     ns - nanoseconds (8 bytes)
#
#   ts_curr - current time
#     sec - seconds (8 bytes)
#     ns - nanoseconds (8 bytes)
#
#   sockfd - socket file descriptor (8 bytes)
#
#   pvm - local protected VM address information
#     addr
#       af - address format (4 bytes)
#       a - IP v4/v6 address (16 bytes)
#     port - port number (2 bytes)
#
#   remote - remote endpoint address information
#     addr
#       af - address format (4 bytes)
#       a - IP v4/v6 address (16 bytes)
#     port - port number (2 bytes)
#
#   bytes_in - total bytes received by the PVM on this connection (8 bytes)
#
#   bytes_out - total bytes sent by the PVM on this connection (8 bytes)
#
#   extra - new socket fd on accept message (8 bytes)
#
#
# mw_feature_request_t (46 bytes)
#
#   base
#     sig - netflow signature (2 bytes)
#     id - message id (4 bytes)
#
#   flags - request flags (2 bytes)
#
#   name  - socket feature name (2 bytes)
#
#   val  - socket feature argument (16 bytes)
#
#   ident - target identification (20 bytes)
#
#
# mw_feature_response_t (26 bytes)
#
#   base
#     sig - netflow signature (2 bytes)
#     id - message id (4 bytes)
#
#   status - response status (4 bytes)
#
#   val - return data (16 bytes)
#

# Beginning of every message - mw_base_t (6 bytes)
BASE_FMT = "!HI"
BASE_FMT_SIZE = struct.calcsize(BASE_FMT)

# IP address network byte ordering as stored in sockaddr - mw_addr_t (20 bytes)
ADDR_FMT = "I" + "16s"

# IP address and port - mw_endpoint_t (22 bytes)
ENDPOINT_FMT = ADDR_FMT + "H"

# Netflow information - mw_netflow_info_t without mw_base_t (110 bytes)
INFO_FMT = "!HQQQQQ" + ENDPOINT_FMT + ENDPOINT_FMT + "QQQ"
INFO_FMT_SIZE = struct.calcsize(INFO_FMT)

# Feature request - mw_feature_request_t (46 bytes)
FEATURE_REQ_FMT = BASE_FMT + "H" + "H" + "Q" + "Q" + "Q" + "12s"

# Feature response - mw_feature_response_t (20 bytes)
FEATURE_RES_FMT = "!i16s"
FEATURE_RES_FMT_SIZE = struct.calcsize(FEATURE_RES_FMT)

# Message signature
SIG_NF_INFO = 0xd310
SIG_FEA_REQ = 0xd320
SIG_FEA_RES = 0xd32f

# Request flags
MW_FEATURE_FLAG_READ    = 0x0
MW_FEATURE_FLAG_WRITE   = 0x1
MW_FEATURE_FLAG_BY_SOCK = 0x2

# Feature message names
FEATURES = dict(
    MtSockAttribNone           = (0, bool),

    # *** Non-socket related commands

    # Turn on traffic monitoring for channel
    MtChannelTrafficMonitorOn  = (0x0001, None),

    # Turn off traffic monitoring for channel
    MtChannelTrafficMonitorOff = (0x0002, None),

    # *** Per-socket features: name => (value, argtype)

    MtSockAttribOpen           = (0x0101, bool), # arg: bool
    MtSockAttribOwnerRunning   = (0x0102, bool), # arg: bool

    MtSockAttribSndBuf         = (0x0109, int), # arg: sz in bytes (uint32_t)
    MtSockAttribRcvBuf         = (0x010a, int), # arg: sz in bytes (uint32_t)

    MtSockAttribSndTimeo       = (0x010b, tuple), # arg: (s,us) tuple
    MtSockAttribRcvTimeo       = (0x010c, tuple), # arg: (s,us) tuple

    MtSockAttribSndLoWat       = (0x010d, int), # arg: sz in bytes (uint32_t)
    MtSockAttribRcvLoWat       = (0x010e, int), # arg: sz in bytes (uint32_t)

    # *** INS-wide changes

    # Change congestion control algo, arg: see vals below
    MtSockAttribSystemInsCongctl    = (0x20, str), # arg: mw_congctl_arg_val_t (uint32_t)

    # Change of delay ACK ticks: arg is signed tick differential
    MtSockAttribSystemDelackTicks   = (0x21, int),  # arg: tick count (uint32_t)
)

##
## Globals
##

outstanding_requests = dict()
open_socks = dict()
MAX_ID = 0xffffffff
curr_id = random.randint(0, MAX_ID)


#####
# Read data from connection until we have a complete packet
# in :
#   sock - (socket object)
#   size - (int) number of bytes to read
# out: (ascii str) data read from socket
#
def recvall(sock, size):
    tot = 0
    data = b""
    sock.settimeout(0.0)

    while tot < size:
        d = sock.recv(size - tot)
        if not len(d):
            break
        tot += len(d)
        data += d

    return data


#####
# Decode IP address information
# in :
#   af   - (int) address format
#   ip   - (str) ip address
#   port - (str) port number
# out: (str) <ip address>:<port number>
#
def endpoint(af, ip, port): 
    if af == 4:
        addr = socket.inet_ntop(socket.AF_INET, ip[:4])
    elif af == 6:
        addr = '[' + socket.inet_ntop(socket.AF_INET6, ip) + ']'
    else:
        raise RuntimeError("Invalid IP address family")

    return "{0}:{1}".format(addr, port)


#####
# Retrieve netflow traffic message name
# in :
#   obs - (int) message number
# out: (str) message name
#
def get_obs_str(obs):
    return { 0 : "none",
             1 : "create",
             2 : "bind",
             3 : "accept",
             4 : "connect",
             5 : "recv",
             6 : "send",
             7 : "close"}.get(obs, "invalid")


#####
# Obtain next unique message ID
# in : None
# out: (int) message id
#
def get_next_id():
    global curr_id
    curr_id += 1
    while curr_id in outstanding_requests:
        curr_id += 1
        curr_id %= MAX_ID

    outstanding_requests[curr_id] = None

    return curr_id


#####
# Obtain netflow server connection information.
# in : None
# out: (str) <ip_addr>:<port_num>
#
def server_info():
    try:
        with pyxs.Client() as c:
            server = c[MW_XENSTORE_NETFLOW]
    except Exception as e:
        print("XenStore path: {}".format(MW_XENSTORE_NETFLOW))
        raise

    return server


#####
# Create a connection to the netflow server
# in :
#   server - (str) server ip and port number
# out: success - (socket object)
#      failure - None
#
def server_connect(server):
    if server.find("[", 0, 2) == -1:
        ip, port = server.split(':') # IPv4
    else:
        new_ip, port = server.rsplit(':', 1) # IPv6
        ip = new_ip.strip('[]')

    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    except socket.error as e:
        sock = None

    try:
        sock.connect((ip, int(port, 10)))
    except socket.error as e:
        sock.close()
        sock = None

    return sock

#####
# Retrieve and process any data available on the socket 
# in :
#   sock - (socket object)
# out: (dict) - message data
#      None - no data available during the timeout period
#
def get_msg(sock):
    msg = {}
    sock.settimeout(1.0)
    try:
        data = sock.recv(BASE_FMT_SIZE)
        sig, ident = struct.unpack(BASE_FMT, data)
        if sig == SIG_NF_INFO:
            msg = process_info_netflow(sock)
        elif sig == SIG_FEA_RES:
            msg = process_response(sock, ident)
        else:
            msg['mtype'] = "unknown"
            msg['sig'] = sig
    except socket.error as e:
        msg = None   # No data available on socket

    return msg


#####
# Process and print the received netflow data
# in :
#   sock - (socket object)
# out: dict
#   mtype   - (str) "information" - protected application traffic data
#   age     - (float) seconds since connection was established 
#   sockfd  - (int) socket file description
#   pvm     - (str) local endpoint address
#   remote  - (str) remote endpoint address
#   obs     - (str) observed syscall
#   inbytes - (int) total bytes received by PVM on this connection
#   outbytes- (int) total bytes sent by PVM on this connection
#   extra   - (int) new socket file descriptor for accept syscall
#
def process_info_netflow(sock):
    try:
        raw = recvall(sock, INFO_FMT_SIZE)
        vals = struct.unpack(INFO_FMT, raw)
    except Exception as e:
        msg = dict( mtype = "error", msg  = e)
        return msg

    (o, start_sec, start_ns, curr_sec, curr_ns) = vals[:5]
    (sockfd) = vals[5]
    (pf, pa, pp) = vals[6:9]
    (rf, ra, rp) = vals[9:12]
    (inbytes, outbytes, extra) = vals[12:]

    start_time = start_sec + start_ns / 1000000000.
    event_time = curr_sec  + curr_ns  / 1000000000.
    age = event_time - start_time
    remote = endpoint(rf, ra, rp)
    pvm = endpoint(pf, pa, pp)
    obs = get_obs_str(o)

    msg = dict(
        mtype    = "information",
        age      = age,
        sockfd   = sockfd,
        pvm      = pvm,
        remote   = remote,
        obs      = obs,
        inbytes  = inbytes,
        outbytes = outbytes,
        extra    = extra,
        )

    return msg


#####
# Receive and process the response data
# in :
#   sock  - (socket object) connection socket
#   ident - (int) message id
# out: (dict)
#   mtype    - (str) "information" - protected application traffic data
#   age      - (float) seconds since connection was established 
#   sockfd   - (int) socket file description
#   pvm      - (str) local endpoint address
#   remote   - (str) remote endpoint address
#   obs      - (str) observed syscall
#   inbytes  - (int) total bytes received by PVM on this connection
#   outbytes - (int) total bytes sent by PVM on this connection
#   extra    - (int) new socket file descriptor for accept syscall
#
def process_response(sock, ident):
    data = recvall(sock, FEATURE_RES_FMT_SIZE)

    if len(data) != FEATURE_RES_FMT_SIZE:
        msg = dict(
            mtype = "error",
            msg = "data length mismatch {0} != {1}".format(
                len(data), FEATURE_RES_FMT_SIZE)
            )
        return msg

    status, val = struct.unpack(FEATURE_RES_FMT, data)
    state = outstanding_requests[ident]
    del outstanding_requests[ident]
    vals = struct.unpack("!QQ", val)

    msg = dict(
        mtype = "response",
        ident = ident,
        status = status,
        vals0 = vals[0],
        vals1 = vals[1],
        )

    return msg


def send_feature_request(conn, sockfd, name, value, flag):
    ''' Send netflow request '''
    ident = get_next_id()

    outstanding_requests[ident] = {
        'mtype' : 'feature',
        'flag' : flag,
        'sockfd': sockfd,
        'name'  : name,
        'value' : value
        }

    req = struct.pack(FEATURE_REQ_FMT, SIG_FEA_REQ, ident,
        flag, name, value[0], value[1], sockfd, "")

    conn.sendall(req)


if __name__ == '__main__':

    # Example usage of netflow library and interface

    import time

    # Required for pyxs access to XenStore
    if os.geteuid() != 0:
        print("You need to have root privileges to run this script")
        sys.exit(1)

    # Establish connection to mwcomms netflow
    nf_server = server_info()
    nf_sock = server_connect(nf_server)

    #print("*** Established netflow connection {}".format(nf_server))

    #print("*** Monitor connection for messages and send requests")

    # Monitor traffic for 60 secs and change monitoring every 10 secs
    r = 0
    st = mt = time.time()
    while True:
        msg = get_msg(nf_sock)
        if msg != None:
            print(msg)
        if time.time() - st  > 3000000:
            break

    print("*** Cleanup and exit")
    nf_sock.close
    sys.exit(0)

