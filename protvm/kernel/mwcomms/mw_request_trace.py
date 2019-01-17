#!/usr/bin/env python3
#
# Read, parse and print out the mwcomms request tracing data.
#

import sys
import os
import argparse
import logging

# mt_request_type_t (mwsocket_trace_buffer_t->type field)
request_type = {
    '0' : 'Invalid',
    '1' : 'SocketCreate',
    '2' : 'SocketShutdown',
    '3' : 'SocketClose',
    '16': 'SocketConnect',
    '17': 'SocketBind',
    '18': 'SocketListen',
    '19': 'SocketAccept',
    '32': 'SocketSend',
    '33': 'SocketRecv',
    '34': 'SocketRecvFrom',
    '48': 'SocketGetName',
    '49': 'SocketGetPeer',
    '50': 'SocketAttrib',
    '51': 'PollsetQuery'
}

# mt_request_type_t (mwsocket_trace_buffer_t->fops field)
request_fops = {
    '0': 'none',
    '1': 'read',
    '2': 'write',
    '3': 'ioctl',
    '4': 'poll',
    '5': 'release'
}

log_levels = dict(
    critical = 50,
    error    = 40,
    warning  = 30,
    info     = 20,
    debug    = 10
)

#
# Globals
#

mwcomms_request_trace = '/sys/kernel/debug/mwcomms/request_trace'

# Get time difference
# Return 0 if:
#   - start value is zero
#   - end value is zero
#   - start value is greater than end value
def get_diff(end, start):
    if start == 0 or end == 0 or start > end:
        return 0
    else:
        return end - start

def display_data(mwr, df):

    # mwr list fields
    # [0]  index;  // buffer index
    # [1]  pid;    // actreq->issuer = current;
    # [2]  fops;   // file operation entry point (read(1), write(2), ioctl(3), poll(4), release(5) or none(0))
    # [3]  type;   // request type (Request->base.type)
    # [4]  t_mw1;  // nanosecond timestamp at fops entry (0 if internal cmd)
    # [5]  t_mw2;  // nanosecond timestamp at request creation
    # [6]  t_mw3;  // nanoseconds timestamp at attempt to get ring buffer slot
    # [7]  t_mw4;  // nanoseconds timestamp at request on ring buffer
    # [8]  t_mw5;  // nanoseconds timestamp at request off ring buffer
    # [9]  t_mw6;  // nanoseconds timestamp at request destruction
    # [10] t_ins;  // nanoseconds elapsed time in ins
    # [11] t_cnt;  // transaction count per fops

   if df == 'raw':
       # Print raw data
       for req in mwr:
           print("{}".format(req))

   if df == 'splits':
       # Print formatted data
       for req in mwr:
           try:
               print('[{0}] pid = {1}, fops = {2}, type = {3}, '
                     'cnt = {11}, t2-t1 = {4}, t3-t2 = {5}, '
                     't4-t3 = {6}, t5-t4 = {7}, t6-t5 = {8}, '
                     'ins = {9}, t5-t4-ins = {10}'.format(
                     req[0],
                     req[1],
                     request_fops[str(req[2])],
                     request_type[str(req[3])],
                     get_diff(req[5], req[4]),
                     get_diff(req[6], req[5]),
                     get_diff(req[7], req[6]),
                     get_diff(req[8], req[7]),
                     get_diff(req[9], req[8]),
                     req[10],
                     get_diff(get_diff(req[8], req[7]), req[10]),
                     req[11]))
           except:
               # TODO: some entries get mangled during copy from mwcomms
               print('[mangled] {}'.format(req))


if __name__ == '__main__':

    #
    # Command line arguments
    #

    parser = argparse.ArgumentParser()

    parser.add_argument(
        '-g',
        action='store',
        dest='log_level',
        type=str,
        choices=['critical', 'error', 'warning', 'info', 'debug'],
        default='info',
        help='Logging level (default: %(default)s)')

    parser.add_argument(
        '-f',
        action='store',
        dest='display_format',
        type=str,
        choices=['raw', 'splits'],
        default='raw',
        help='Display format (default: %(default)s)')

    parser.add_argument(
        '-n',
        action='store',
        dest='display_lines',
        type=int,
        default='0',
        help='Number of lines to display, zero means all lines (default: %(default)s)')

    args = parser.parse_args()

    if os.geteuid() != 0:
        print("Must have root privileges to run this script")
        sys.exit(1)

    # Sanity check command line arguments

    # Check prerequisites

    logging.basicConfig(format='%(levelname)s: %(message)s',
        level=log_levels[args.log_level])

    # Read in request trace data
    mw_requests = []
    with open(mwcomms_request_trace, 'r') as f:
        while True:
            line = f.readline()
            if not line:
                break
            req = [int(x) for x in line.rstrip('\n').split(':')]
            mw_requests.append(req)
            if args.display_lines != 0 and len(mw_requests) >= args.display_lines:
                break

    display_data(mw_requests, args.display_format)

