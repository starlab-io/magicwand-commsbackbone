#!/usr/bin/env python2

import os
import sys
import threading
import signal
import termios
import tty
import select
import mw_netflow

outstanding_requests = dict()
open_socks = dict()
info_display = True
info_muted = False
prg_run = True


class SignalHandler(object):
    def setup(self, sock, term_attr, thread_p):        
        self.sock = sock
        self.term_attr = term_attr
        self.thread_p = thread_p
        signal.signal(signal.SIGINT, self.catch)

    def catch(self, signum, frame):
        cleanup(self.sock, self.term_attr, self.thread_p)


def cleanup(sock, term_attr, thread_p):
    print('*** Cleanup and exit ***')

    global prg_run
    termios.tcsetattr(sys.stdin, termios.TCSADRAIN, term_attr)
    prg_run = False
    thread_p.join()
    sock.close
    sys.exit(0)

def isData():
    return select.select([sys.stdin], [], [], 0) == ([sys.stdin], [], [])


def b2h(bytes):
    """ Convert byte string to hex ASCII string """
    return ' '.join([ '{0:02x}'.format(ord(y)) for y in bytes ])


def netflow_monitor(conn):
    global prg_run
    global info_muted

    while prg_run:
        msg = mw_netflow.get_msg(nf_sock)
        if msg != None:
            if not info_muted:
                print(msg)
            if msg['mtype'] == "information":
                if msg['obs'] == "accept":
                    open_socks[msg['extra']] = msg['remote']
                if msg['obs'] == "close":
                    if msg['extra'] in open_socks.keys():
                        del open_socks[msg['extra']]


def input_monitor(conn, term_attr):
    global info_display
    global info_muted
    global prg_run

    osa = []

    print('*** Type \'h\' for help menu ***')

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
                print("p - print open sockets")
                print("m - netflow information monitor un-muted")
                print("M - netflow information monitor muted")
                print("o - netflow information monitor on (enables open socket list")
                print("O - netflow information monitor off (disables open socket list)")
                print("c - close socket (mitigation)")
            elif c == '\x70':    # 'p'
                if info_display:
                    print("*** Open socket list ***")
                    for sock in open_socks.keys():
                        print("socket 0x{0:x}/{0} --> remote {1}".format(sock, open_socks[sock]))
                else:
                    print("*** Open socket list (disabled when traffic monitor is off) ***")
            elif c == '\x6d':    # 'm'
                info_muted = False
                print("*** NetFlow Information Display (Un-Muted) ***")
            elif c == '\x4d':    # 'M'
                info_muted = True
                print("*** NetFlow Information Display (Muted) ***")
            elif c == '\x6f':    # 'o'
                mw_netflow.send_feature_request(conn, 0,
                    mw_netflow.FEATURES['MtChannelTrafficMonitorOn'][0],
                    (0,0), mw_netflow.MW_FEATURE_FLAG_READ)
                info_display = True
                print("*** NetFlow Information Display (On) ***")
            elif c == '\x4f':    # 'O'
                mw_netflow.send_feature_request(conn, 0, mw_netflow.FEATURES['MtChannelTrafficMonitorOff'][0],
                    (0,0), mw_netflow.MW_FEATURE_FLAG_READ)
                info_display = False
                open_socks.clear()
                print("*** NetFlow Information Display (Off) ***")
            elif c == '\x63':    # 'c'
                del osa[:]
                if not open_socks:
                    print("*** No open sockets ***")
                    continue
                print("{0:>2d}) exit without closing socket".format(0))
                for sock in open_socks.keys():
                    osa.append(sock)
                    print("{0:>2d}) socket 0x{1:x}/{1} --> remote {2}".format(len(osa), sock, open_socks[sock]))

                termios.tcsetattr(sys.stdin, termios.TCSADRAIN, term_attr)
                try:
                    sock_index = int(raw_input("Index of socket to close: "))
                    if sock_index == 0:
                        continue
                    elif not (1 <= sock_index <= len(osa)):
                        raise ValueError()
                except ValueError:
                        print "*** Invalid option, you needed to enter a valid index ***"
                else:
                    kill_sockfd = osa[sock_index-1]
                    print("*** Closing open socket 0x{0:x} ***".format(kill_sockfd))
                    mw_netflow.send_feature_request(conn, kill_sockfd,
                        mw_netflow.FEATURES[ 'MtSockAttribOpen' ][0], (0,0),
                        mw_netflow.MW_FEATURE_FLAG_WRITE + mw_netflow.MW_FEATURE_FLAG_BY_SOCK)
                tty.setcbreak(sys.stdin.fileno())


if __name__ == '__main__':
    if os.geteuid() != 0:
        print("You need to have root privileges to run this script")
        sys.exit(1)

    # Establish connection to mwcomms netflow
    nf_server = mw_netflow.server_info()
    nf_sock = mw_netflow.server_connect(nf_server)

    print("*** Established netflow connection {} ***".format(nf_server))

    term_attr = termios.tcgetattr(sys.stdin)
    tty.setcbreak(sys.stdin.fileno())

    thread_p = threading.Thread(name='input monitor', target=input_monitor, args=(nf_sock, term_attr))
    thread_p.start()

    sig_handler = SignalHandler()
    sig_handler.setup(nf_sock, term_attr, thread_p)

    netflow_monitor(nf_sock)

    cleanup(nf_sock, term_attr, thread_p)

