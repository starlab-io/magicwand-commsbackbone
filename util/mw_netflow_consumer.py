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
    print('\n*** Cleanup and exit ***')

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

    while prg_run:
        msg = mw_netflow.get_msg(nf_sock)
        if msg != None:
            print(msg)


def input_monitor(conn):
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
                mw_netflow.send_feature_request(conn, 0,
                    mw_netflow.FEATURES[ 'MtChannelTrafficMonitorOn' ][0], None)
                info_display = True
                print("*** NetFlow Information Display (On) ***")
            elif c == '\x4f':    # 'O'
                mw_netflow.send_feature_request(conn, 0,
                  mw_netflow.FEATURES[ 'MtChannelTrafficMonitorOff' ][0], None)
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

    # Establish connection to mwcomms netflow
    nf_server = mw_netflow.server_info()
    nf_sock = mw_netflow.server_connect(nf_server)

    print("*** Established netflow connection {}".format(nf_server))

    term_attr = termios.tcgetattr(sys.stdin)
    tty.setcbreak(sys.stdin.fileno())

    thread_p = threading.Thread(name='input monitor', target=input_monitor, args=(nf_sock,))
    thread_p.start()

    sig_handler = SignalHandler()
    sig_handler.setup(nf_sock, term_attr, thread_p)

    netflow_monitor(nf_sock)

    cleanup(nf_sock, term_attr, thread_p)

