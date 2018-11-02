#!/usr/bin/env python2
#
# Front-end glue for MagicWand that spawns Rump INSs and routes incoming
# connections to a specific instance based on runtime variables.
#
# Assumptions:
#   - No INS instances currently running
#   - TCP/IP Forwarding has been enabled
#   - Xen keys cleared
#   - mwcomms driver loaded
#   - start this script, then start apache, then start testing
#
# Iptables debug:
#   $ sudo iptables -v --line-numbers -t nat -n -L PREROUTING  # show NAT PREROUTINE chain
#   $ sudo iptables -v --line-numbers -t filter -n -L FORWARD  # show FILTER FORWARD chain
#
# Package requirements:
#   python-iptables: pip install --upgrade python-iptables
#   pyxs           : pip install --upgrade pyxs
#
# Other notes:
#   The XenStore event watcher can block forever if the PVM driver
#   isn't loaded or in the right state. See
#   https://bugs.python.org/issue8844 and the pyxs source (client.py)
#   for details.
#
#   XenAPI is broken on Ubuntu 14.04/Xen 4.4, so we don't use it. We
#   interface with Xen by calling out to xl.
#
#   The INS memory requirement is based on the number of sockets it
#   supports. Take this into account here (manually or programmatically).
#

import logging
import sys
import os
import random
import signal
import time
import re
import subprocess
import httplib
import xmlrpclib
import socket
import thread
import threading
import iptc # iptables bindings
import pyxs
import argparse

# Broken on Ubuntu 14.04
# import xen.xm.XenAPI as XenAPI

# See http://libvirt.org/docs/libvirt-appdev-guide-python
# import libvirt # system-installed build doesn't support new XenAPI

#
# Constants for RUMP/NetBSD socket and TCP options that we may need.
#

# Per-socket options: use these for mitigation

"""
include/sys/socket.h


/*
 * Additional options, not kept in so_options.
 */
#define SO_SNDBUF       0x1001          /* send buffer size */
#define SO_RCVBUF       0x1002          /* receive buffer size */
#define SO_SNDLOWAT     0x1003          /* send low-water mark */
#define SO_RCVLOWAT     0x1004          /* receive low-water mark */
/* SO_OSNDTIMEO         0x1005 */
/* SO_ORCVTIMEO         0x1006 */
#define SO_ERROR        0x1007          /* get error status and clear */
#define SO_TYPE         0x1008          /* get socket type */
#define SO_OVERFLOWED   0x1009          /* datagrams: return packets dropped */

#define SO_NOHEADER     0x100a          /* user supplies no header to kernel;
                                         * kernel removes header and supplies
                                         * payload
                                         */
#define SO_SNDTIMEO     0x100b          /* send timeout */
#define SO_RCVTIMEO     0x100c          /* receive timeout */


TCP system-wide options: use these for diversification
-----------------------
net.inet.tcp.recvbuf_auto: 1
net.inet.tcp.recvspace:    32768
net.inet.tcp.recvbuf_inc:  16384
net.inet.tcp.recvbuf_max:  262144
net.inet.tcp.sendbuf_auto: 1
net.inet.tcp.sendspace:    32768
net.inet.tcp.sendbuf_inc:  8192
net.inet.tcp.sendbuf_max:  262144
net.inet.tcp.init_win:     4
net.inet.tcp.init_win_local: 4
net.inet.ip.ifq.maxlen:      256
net.inet.tcp.delack_ticks:   20
net.inet.tcp.congctl.selected:  newreno
net.inet.tcp.congctl.available: reno newreno cubic

"""

#
# Globals
#

MW_XENSTORE_ROOT = b"/mw"

mwroot_current = ''
mwroot_branch = ''

# Shared values from common C header file
path_common_config_h ='/common/common_config.h'
max_ins_count_v = 0
heartbeat_interval_sec_v = 0
heartbeat_max_misses_v = 0

# Load percent at which INS is considered overloaded
max_ins_load_percent = 0.0

# Monitoring message interval (0 is disabled)
monitor_interval = 0

# INS overloaded trigger file
ins_overloaded_trigger = None

# Startup all INS instances at beginning instead of on demand
start_all_ins_instances = False

INS_MEMORY_MB = 3048
POLL_INTERVAL = 0.05
DEFAULT_TIMEOUT = 10 

log_levels = dict(
    critical = 50,
    error    = 40,
    warning  = 30,
    info     = 20,
    debug    = 10
)

# Maximum INS load (triggers load balancing)
MAX_INS_LOAD_DEFAULT = 50

# Monitor message interval in seconds
MONITOR_PERIOD_DEFAULT = 5

# Map: domid => INS instance
ins_map = dict()

# List of INS's that do not have domids assigned
ins_queue = list()

# MACs for usage by INSs; this way we don't overflow DHCP's mappings
macs = { '00:16:3e:28:2a:50' : { 'in_use' : False },
         '00:16:3e:28:2a:51' : { 'in_use' : False },
         '00:16:3e:28:2a:51' : { 'in_use' : False },
         '00:16:3e:28:2a:52' : { 'in_use' : False },
         '00:16:3e:28:2a:53' : { 'in_use' : False },
         '00:16:3e:28:2a:54' : { 'in_use' : False },
         '00:16:3e:28:2a:55' : { 'in_use' : False },
         '00:16:3e:28:2a:56' : { 'in_use' : False },
         '00:16:3e:28:2a:57' : { 'in_use' : False },
         '00:16:3e:28:2a:58' : { 'in_use' : False },
         '00:16:3e:28:2a:59' : { 'in_use' : False },
         '00:16:3e:28:2a:5a' : { 'in_use' : False },
         '00:16:3e:28:2a:5b' : { 'in_use' : False },
         '00:16:3e:28:2a:5c' : { 'in_use' : False },
         '00:16:3e:28:2a:5d' : { 'in_use' : False },
         '00:16:3e:28:2a:5e' : { 'in_use' : False },
         '00:16:3e:28:2a:5f' : { 'in_use' : False },
         '00:16:3e:28:2a:60' : { 'in_use' : False },
         '00:16:3e:28:2a:61' : { 'in_use' : False },
         '00:16:3e:28:2a:62' : { 'in_use' : False },
         '00:16:3e:28:2a:63' : { 'in_use' : False },
         '00:16:3e:28:2a:64' : { 'in_use' : False },
         '00:16:3e:28:2a:65' : { 'in_use' : False },
         '00:16:3e:28:2a:66' : { 'in_use' : False },
         '00:16:3e:28:2a:67' : { 'in_use' : False },
         '00:16:3e:28:2a:68' : { 'in_use' : False },
         '00:16:3e:28:2a:69' : { 'in_use' : False },
         '00:16:3e:28:2a:6a' : { 'in_use' : False },
         '00:16:3e:28:2a:6b' : { 'in_use' : False },
         '00:16:3e:28:2a:6c' : { 'in_use' : False },
         '00:16:3e:28:2a:6d' : { 'in_use' : False },
         '00:16:3e:28:2a:6e' : { 'in_use' : False },
         '00:16:3e:28:2a:6f' : { 'in_use' : False } }

inst_num = 0

exit_requested = False

def do_exit(status):
    global exit_requested
    exit_requested = True
    sys.exit(status)

def handler(signum, frame):
    global exit_requested
    logging.warn("Caught signal {0}".format(signum))
    exit_requested = True


def generate_sys_net_opts():
    """ Randomly, but smartly generate INS network configuration. """
    params = list()

    # Calc send, recv settings
    for prefix in ("send", "recv"):
        bufauto = random.randint(0, 1)
        params.append("{0}buf_auto:{1}".format(prefix, bufauto))

        #if bufauto:
        #    continue

        # initial buffer size
        #bufspace = random.randrange(0x1000, 0x40001, 0x1000)
        bufspace = random.choice(xrange(0x1000, 0x40001, 0x1000))
        params.append("{0}space:{1:x}".format(prefix, bufspace))

        # buffer increment size
        #bufinc = random.randrange bufspace / 4, bufspace / 2, 0x800)
        bufinc = random.choice(xrange(bufspace/4, bufspace/2, 0x800))
        params.append("{0}buf_inc:{1:x}".format(prefix, bufinc))

        # max buffer size
        #bufmax = random.randrange(bufspace, bufspace * 4, 0x1000)
        bufmax = random.choice(xrange(bufspace, bufspace * 4, 0x1000))
        params.append("{0}buf_max:{1:x}".format(prefix, bufmax))

        assert bufmax >= bufspace, "nonsensical space vs max values"

    # Calc other settings
    params.append("init_win:{0:x}".format(random.randint(2, 6)))
    params.append("init_win_local:{0:x}".format(random.randint(2, 6)))
    params.append("delack_ticks:{0:x}".format(random.randint(10, 40)))
    params.append("congctl:{0}".format(random.choice(["reno", "newreno", "cubic"])))

    logging.debug("TCP/IP parameters:\n\t{0}".format("\n\t".join(params)))
    opts = " ".join(params)
    return opts


# Generic storage for INS
class INS:
    def __init__(self, domid=None):
        self.domid              = domid
        self.ip                 = None
        self.stats              = dict()
        self._last_contact      = time.time()
        self._missed_heartbeats = 0
        self._lock              = threading.Lock()
        self._ins_forwarders    = list()
        self._active            = False
        self._overloaded        = False

        if domid == None:
            self._create()

    def __del__(self):
        """ Destroy the INS associated with this object. """
        if self.domid == None:
           return

        p = subprocess.Popen(["xl", "destroy", "{0:d}".format(self.domid)],
                              stdout = subprocess.PIPE, stderr = subprocess.PIPE)
        (stdout, stderr) = p.communicate()
        rc = p.wait()
        if rc:
            raise RuntimeError("Call to xl failed: {}\n".format(stderr))

        macs[self._mac]['in_use'] = False

        logging.info("Destroyed INS {}".format(self))

    def __str__(self):
        rules = ""
        if self._ins_forwarders:
            rules = "\n\t" + "\n\t".join([str(f) for f in self._ins_forwarders])

        return ("id {} IP {} {}active{}".
                format(self.domid, self.ip,
                        { False : "in", True : "" }[self._active], rules))

    def __repr__(self):
        return str(self)

    def _create(self):
        """
        Spawn the Rump INS domU. Normally this is done via rumprun, which
        runs 'xl create xr.conf', where xr.conf looks like this:

        kernel="ins-rump.run"
        name="mw-ins-rump"
        vcpus=1
        memory=256
        on_poweroff="destroy"
        on_crash="destroy"
        vif=['xenif']

        --------------------

        Spawn the Rump INS domU. Normally this is done via rumprun like this:

        rumprun -T /tmp/rump.tmp -S xen -di -M 256 -N mw-ins-rump \
        -I xen0,xenif \
        -W xen0,inet,static,$RUMP_IP/8,$_GW \
        ins-rump.run
        """

        global inst_num
        global mwroot_branch

        # Find available MAC (random or sorted)
        #mac = random.sample([k for k,v in macs.items() if not v['in_use']], 1)
        mac = sorted([k for k,v in macs.items() if not v['in_use']])
        if not mac:
            raise RuntimeError("No more MACs are available")
        mac = mac[0]
        macs[mac]['in_use'] = True
        self._mac = mac

        RUMP_RUN_CMD  = '{0}/ins/ins-rump/rumprun{1}/bin/rumprun'.format(mwroot_current, mwroot_branch)
        RUMP_RUN_FILE = '{0}/ins/ins-rump/apps/ins-app/ins-rump.run'.format(mwroot_current)

        if not os.path.isfile(RUMP_RUN_CMD):
            print("Rump run cmd does not exist: {}\n".format(RUMP_RUN_CMD))
            do_exit(1)
        if not os.path.isfile(RUMP_RUN_FILE):
            print("Rump run file does not exist: {}\n".format(RUMP_RUN_FILE))
            do_exit(1)

        # We will not connect to the console (no "-i") so we won't wait for exit below.
        cmd  = '{} -S xen -d '.format(RUMP_RUN_CMD)
        cmd += '-M {} '.format(INS_MEMORY_MB)
        cmd += '-N mw-ins-rump-{:04x} '.format(inst_num)
        cmd += '-I xen0,xenif,mac={} '.format(mac)
        cmd += '-W xen0,inet,dhcp {}'.format(RUMP_RUN_FILE)

        inst_num += 1

        logging.debug("Running command {0}".format(cmd))

        try:
            p = subprocess.Popen(cmd.split(), stdout = subprocess.PIPE, stderr = subprocess.PIPE)
        except Exception as e:
            print("Failed to start INS: {}\n".format(str(e)))
            do_exit(1)

        #(stdout, stderr) = p.communicate()
        #rc = p.wait()

        t = 0
        while True:
            rc = p.poll()
            if exit_requested:
                break
            if rc is not None:
                break
            if t >= DEFAULT_TIMEOUT:
                raise RuntimeError("Call to xl took too long: {0}\n".format(p.stderr.read()))
            else:
                time.sleep(POLL_INTERVAL)
                t += POLL_INTERVAL

        if p.returncode:
            raise RuntimeError("Call to xl failed: {0}\n".format(p.stderr.read()))

        # self.domid = int(p.stdout.read().split(':')[1])
        ins_queue.append(self)

    def set_listening_ports(self, port_list):
        self.lock()
        try:
            for p in port_list:
                if p in [x.get_port() for x in self._ins_forwarders]:
                    continue
                fwd = PortForwarder(p, self.ip)
                self._ins_forwarders.append(fwd)
        finally:
            self.unlock()

    def set_ins_active(self, activated):

        # Do not activate an INS without listeners
        if activated == True and not self._ins_forwarders:
            return

        self.lock()
        try:
            for f in self._ins_forwarders:
                f.set_pf_active(activated)
        finally:
            self.unlock()
        self._active = activated

        if activated == False:
            self._overloaded = False

        logging.info("{0} INS {1}".format(
            'Activated' if activated else 'Deactivated', self.domid))

    def is_active(self):
        return self._active

    def register_heartbeat(self):
        self.lock()
        try:
            self._last_contact = time.time()
            self._missed_heartbeats = 0
        finally:
            self.unlock()

    def check_heartbeat(self):
        """ False ==> this INS is dead. """
        global heartbeat_interval_sec_v
        global heartbeat_max_misses_v
        alive = True
        self.lock()
        try:
            if(time.time() - self._last_contact >
                heartbeat_interval_sec_v * (self._missed_heartbeats + 1) + 1):
                self._missed_heartbeats += 1
                logging.warn("INS {} has missed {} heartbeat(s)\n".
                              format(self.domid, self._missed_heartbeats))

                if self._missed_heartbeats >= heartbeat_max_misses_v:
                    logging.warn("INS {} is now considered dead\n".format(self.domid))
                    alive = False
        finally:
            self.unlock()

        return alive

    def lock(self):
        self._lock.acquire()

    def unlock(self):
        self._lock.release()

    def update_stats(self, stats_str):
        self.lock()
        try:
            # See heartbeat_thread_func in xenevent.c for this format
            stats = [int(x,16) for x in stats_str.split(':')]
            self.stats['max_sockets']  = stats[0]
            self.stats['used_sockets'] = stats[1]
            self.stats['recv_bytes']   = stats[2]
            self.stats['sent_bytes']   = stats[3]
        finally:
            self.unlock()
            logging.debug("Stats updated for domid = {}".format(self.domid))
            logging.debug("  max_sockets = {0} used_sockets = {1}".format(
                self.stats['max_sockets'], self.stats['used_sockets']))
            logging.debug("  recv_bytes = {0}, sent_bytes = {1}".format(
                self.stats['recv_bytes'], self.stats['sent_bytes']))

    def get_load(self, print_msg=False):
        global max_ins_load_percent
        used = 1.0 * self.stats.get('used_sockets', 0)
        capacity = 1.0 * self.stats.get('max_sockets', 1)
        if print_msg:
            logging.info("{0:>3}/{1:15}: load = {2:>7.3f}/{3:.3f} | {4:.3f}/{5:.3f} - {6} / {7}".format(
                self.domid, self.ip, used, capacity, used/capacity, max_ins_load_percent,
                'Active' if self._active else 'Not Active',
                'Overloaded' if self.overloaded() else 'Not Overloaded'))
        return used / capacity

    def overloaded(self):
        global max_ins_load_percent
        global ins_overloaded_trigger

        if self._overloaded == True:
            return True

        if ins_overloaded_trigger and os.path.isfile(ins_overloaded_trigger):
            os.remove(ins_overloaded_trigger)
            self._overloaded = True
            return True

        return self.get_load() >= max_ins_load_percent

    def wait(self):
        """ Waits until the INS is ready to use. """

        t = 0.0
        # The watcher must have populated the INS's IP
        while True:
            if (self.domid != None and
                self.domid in ins_map and
                self.ip != None):
                logging.info("INS ready {}".format(self))
                logging.info("All INS instances {}".format(ins_map))
                break

            time.sleep(POLL_INTERVAL)
            t += POLL_INTERVAL
            if t > DEFAULT_TIMEOUT:
                raise RuntimeError("wait() is taking too long")


class PortForwarder:
    """
    Class that manages iptables rules for TCP traffic forwarding;
    supports MagicWand's management of multiple INSs. Each instance of
    this class forwards one port from the Dom0 to the same port on an
    INS.

    NOTE:
    These rules necessitate that incoming connections originate from
    somewhere other than the Dom0 (or associated DomU's ???).

    These rules are needed to forward port 2200 to IP (INS) 1.2.3.4:
    1. iptables -A FORWARD -p tcp --dport 2200 -j ACCEPT

    2. iptables -t nat -I PREROUTING -m tcp -p tcp --dport 2200 \
        -j DNAT --to-destination 1.2.3.4

    3. iptables -A FORWARD -m conntrack --ctstate \
        ESTABLISHED,RELATED -j ACCEPT
    """

    def __init__(self, in_port, dest_ip):
        # List of tuples: (chain, rule). The first is always baseline
        # rule to accept established traffic
        self._rules = list()
        self._port = in_port
        self._dest = dest_ip
        self._active = False

    def set_pf_active(self, activate):
        if self._active == activate:
            return
        if activate:
            self._redirect_conn_to_addr()
        else:
            self._deactivate()
        self._active = activate

    def active(self):
        return self._active

    def _deactivate(self):
        ''' Delete iptable rules from currently inactivated INS '''
        logging.info("Deactivate iptables entries - INS {}".format(self))
        for (t,c,r) in self._rules:
            t.refresh()
            c.delete_rule(r)
            t.refresh()
        del self._rules[:] # drop old refs

    def __del__(self):
        self._deactivate()

    def __str__(self):
        return "*:{0} ==> {1}:{0}".format(self._port, self._dest)

    def get_port(self):
        return self._port

    def _enable_rule(self, t, c, r):
        t.refresh()
        c.insert_rule(r)
        t.refresh()
        self._rules.append((t, c, r))

    def _redirect_conn_to_addr(self):
        """
        Configure an external IP address on the "outside" interface
        and add iptables rule.
        """

        # Forward port 80 to Y.Y.Y.Y:80:
        # iptables -t nat -A PREROUTING -p tcp --dport 80 -j DNAT --to Y.Y.Y.Y:80

        table = iptc.Table(iptc.Table.NAT)
        chain = iptc.Chain(table, "PREROUTING")

        rule = iptc.Rule()
        rule.protocol = "tcp"
        rule.target = iptc.Target(rule, "DNAT")
        rule.target.set_parameter("to_destination", "{0}:{1}".format(self._dest, self._port))

        match = iptc.Match(rule, "comment")
        match.comment = "mw1 {0}".format(self._dest)
        rule.add_match(match)

        match = iptc.Match(rule, "tcp")
        match.dport = "{0:d}".format(self._port)
        rule.add_match(match)

        self._enable_rule(table, chain, rule)

        # Accept traffic directed at the prerouting chain:
        # iptables -A FORWARD -p tcp --dport 80 -j ACCEPT

        table = iptc.Table(iptc.Table.FILTER)
        chain = iptc.Chain(table, "FORWARD")

        rule = iptc.Rule()
        rule.protocol = "tcp"
        rule.target = iptc.Target(rule, "ACCEPT")

        match = iptc.Match(rule, "comment")
        match.comment = "mw2 {0}".format(self._dest)
        rule.add_match(match)

        match = iptc.Match(rule, "tcp")
        match.dport = "{0:d}".format(self._port)
        rule.add_match(match)

        self._enable_rule(table, chain, rule)

        # Prerequisite iptables rule:
        # iptables -A FORWARD -m conntrack -p tcp --ctstate RELATED,ESTABLISHED -j ACCEPT

        table = iptc.Table(iptc.Table.FILTER)
        chain = iptc.Chain(table, "FORWARD")

        rule = iptc.Rule()
        rule.protocol = "tcp"
        rule.target = iptc.Target(rule, "ACCEPT")

        match = iptc.Match(rule, "comment")
        match.comment = "mw3 {0}".format(self._dest)
        rule.add_match(match)

        match = rule.create_match("conntrack")
        match.ctstate = "RELATED,ESTABLISHED"

        self._enable_rule(table, chain, rule)

    def dump(self):

        table = iptc.Table(iptc.Table.FILTER)
        chain = iptc.Chain(table, "FORWARD")

        print("Table {0} / Chain {1}".format(table.name, chain.name))
        for rule in chain.rules:
            print("Rule: proto: {0} src {1} dst {2} iface {3} out {4}".
                format(rule.protocol, rule.src, rule.dst,
                rule.in_interface,rule.out_interface))
            print("Matches:")
            for match in rule.matches:
                print("{0} target {1} comment [{2}]".format(match.name, rule.target.name, match.comment))


class XenStoreEventHandler:
    def __init__(self, xiface):
        self._xiface = xiface

    def event(self, client, path, newval):
        # example path:
        # s.split('/')
        # ['', 'mw', '77', 'network_stats']

        # We should only see the MW root
        assert path.startswith(MW_XENSTORE_ROOT), "Unexpected path {0}".format(path)

        try:
            domid = int(path.split('/')[2])
        except:
            # We can't do anything without the domid
            return

        logging.debug("New event on path: {0} ==> {1}".format(path, newval))

        if 'ins_dom_id' in path:
            assert domid == int(newval)
            if len(ins_queue) == 0:
                ins_map[domid] = INS(domid)
            else:
                ins_queue[0].lock()
                ins_map[domid] = ins_queue.pop(0)
                ins_map[domid].domid = domid
                ins_map[domid].unlock()
        elif 'ip_addrs' in path:
            ips = filter(lambda s: '127.0.0.1' not in s, newval.split())
            assert len(ips) == 1, "too many public IP addresses"
            ins_map[domid].lock()
            ins_map[domid].ip = ips[0]
            ins_map[domid].unlock()
            client[b"/mw/{0}/sockopts".format(domid)] = generate_sys_net_opts()
        elif 'network_stats' in path:
            ins_map[domid].update_stats(newval)
        elif 'heartbeat' in path:
            ins_map[domid].register_heartbeat()
        elif 'listening_ports' in path:
            ports = [int(p, 16) for p in newval.split()]
            ins_map[domid].set_listening_ports(ports)
        else:
            logging.debug("Ignoring event {0} => {1}".format(path, newval))
            pass


class XenStoreWatch(threading.Thread):
    def __init__(self, event_handler):
        threading.Thread.__init__(self)
        self._handler = event_handler

    def handle_xs_change(self, path, newval):
        pass

    def run(self):
        """
        Run the thread. Can block forever and ignore signals in some cases.
        See Other notes at top of file.
        """
        with pyxs.Client() as c:
            m = c.monitor()
            m.watch(MW_XENSTORE_ROOT, b"MW INS watcher")

            events = m.wait()
            if exit_requested:
                return

            for e in events: # blocking is here on generator
                if exit_requested:
                    return

                path = e[0]
                value = None
                if c.exists(path):
                    value = c[path]

                self._handler.event(c, path, value)


class UDSHTTPConnection(httplib.HTTPConnection):
    """ HTTPConnection subclass to allow HTTP over Unix domain sockets. """
    def connect(self):
        path = self.host.replace("_", "/")
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(path)


class UDSHTTP(httplib.HTTPConnection):
    _connection_class = UDSHTTPConnection


class UDSTransport(xmlrpclib.Transport):
    def __init__(self, use_datetime=0):
         self._use_datetime = use_datetime
         self._extra_headers=[]
         self._connection = (None, None)
    def add_extra_header(self, key, value):
         self._extra_headers += [(key,value)]
    def make_connection(self, host):
         # Python 2.4 compatibility
         if sys.version_info[0] <= 2 and sys.version_info[1] < 7:
             return UDSHTTP(host)
         else:
             return UDSHTTPConnection(host)
    def send_request(self, connection, handler, request_body):
         connection.putrequest("POST", handler)
         for key, value in self._extra_headers:
             connection.putheader(key, value)


class XenIface:
     def __init__(self):
         """
         The Xen python bindings are very broken on Ubuntu 14.04, so we
         just call out to the xl program.
         """

     def get_vif(self, domid):
         """ Returns VIF info on the given domain. """

         # These XenAPI methods are present but do not work on Ubuntu
         # 14.04 / Xen 4.4. The info we're getting from them seems to
         # match that under /var/lib/xend/state. Using them *is*
         # preferred over the hackish way we do things now -- too bad
         # they're broken.
         #
         # conn.xenapi.tunnel.get_all()
         # conn.xenapi.VIF.get_all_records()
         # conn.xenapi.network.get_all()
         # conn.xenapi.PIF.get_all_records()
         #
         # This info is also available through XenStore, but pyxs is
         # broken. This code prints "Path: /local/domain/3/device/vif/0 =>"
         #
         # p = b"/local/domain/0"
         # with pyxs.Client() as c:
         #    print("Path: {0} => {1}".format(p, c[p]))

         p = subprocess.Popen(['xl', 'network-list', "{0:d}".format(domid)],
                               stdout = subprocess.PIPE, stderr = subprocess.PIPE)

         (stdout, stderr) = p.communicate()

         rc = p.wait()
         if rc:
             raise RuntimeError("Call to xl failed: {0}".format(stderr))

         # Example stdout
         # Idx BE Mac Addr.         handle state evt-ch   tx-/rx-ring-ref BE-path
         # 0   0  00:00:00:00:00:00 0      4     -1       768/769         /local/domain/0/backend/vif/3/0

         # Extract the info we want, put in dict
         lines = stdout.splitlines()
         assert len(lines) == 2, "Unexpected line count"

         info = lines[1].split()
         bepath = info[7]   # /local/domain/0/backend/vif/3/0
         iface = '.'.join(bepath.split('/')[-3:]) # vif.3.0

         return dict(idx    = info[0],
                     mac    = info[2],
                     state  = info[4],
                     bepath = bepath,
                     iface  = iface)


def print_dict(desc, mydict):
     print(desc)
     for (k,v) in mydict.iteritems():
         print(k)
         print(v)
     print('----------')


def balance_load():
    """
    Serves as load balancer. Routes new connections to least busy INS
    if the current one is too busy. If all of them are too busy, returns
    True so caller will create a new one.
    """

    assert len(ins_map), "No INS is running at all"

    all_active = [v for (k,v) in ins_map.iteritems() if v.is_active()]

    if not all_active:
        # There is no active INS; this can happen on initialization. Pick the first one.
        curr = ins_map.values()[0]
        curr.set_ins_active(True)
    else:
        assert len(all_active) == 1, "Only one INS should be active here"
        curr = all_active[0]

    if not curr.overloaded():
        # Nothing to do here
        return False

    # curr is overloaded: shift burden to the INS with the lowest load
    logging.debug("Stats: {}, load: {}, overloaded: {}".
                   format(curr.stats, curr.get_load(), curr.overloaded()))
    logging.debug("INS {} is overloaded, looking for another one".format(curr))

    available = [v for (k,v) in ins_map.iteritems() if not v.overloaded()]
    if not available:
        logging.info("All INS instances are overloaded".format(curr))
        # Nothing is available - the caller needs to create a new INS
        return True

    # Remove INS instances that have an empty forwarders
    # list (listen event has not happened yet)
    for ins in available:
        if not ins._ins_forwarders:
            available.remove(ins)

    if not available:
        return False

    # All INS instances on available list have listeners

    new = min(available, key=lambda x: x.get_load())

    logging.debug("{} overloaded, directing traffic to {}".format(curr, new))

    new.set_ins_active(True)   # 2 INSs are active
    curr.set_ins_active(False) # 1 INS is active

    return False


def single_ins():
    x = XenIface()
    e = XenStoreEventHandler(x)
    w = XenStoreWatch(e)
    w.start()

    s = INS()
    s.wait() # Wait for INS IP address to appear

    while w.is_alive():
        w.join(POLL_INTERVAL)


def ins_runner():
    """ Spawns INSs as needed, destroys as permissible. """

    global max_ins_count_v
    global monitor_interval
    global start_all_ins_instances
    spawn_new = True
    monitor_cnt = 0.0

    x = XenIface()
    e = XenStoreEventHandler(x)
    w = XenStoreWatch(e)
    w.start()

    # Startup all INS instances now instead of on demand
    if start_all_ins_instances:
        while len(ins_map) < max_ins_count_v:
            i = INS()
            i.wait()
            logging.debug("Spawned new INS instance ({0} of {1})".format(
                len(ins_map), max_ins_count_v))
        spawn_new = False

    # Create more INSs and monitor the workload here.
    # INS activation is handled by balance_load()
    while (not exit_requested):
        if spawn_new and len(ins_map) < max_ins_count_v:
            spawn_new = False
            i = INS()
            i.wait()
            logging.debug("Spawned new INS instance ({0} of {1})".format(
                len(ins_map), max_ins_count_v))

        # Fresh query of INSs in each iteration; the set may have changed.
        # N.B. INSs are inserted into ins_map by the XS monitor.

        for domid in ins_map.keys():
            # Check for death
            if not ins_map[domid].check_heartbeat():
                del ins_map[domid]

        spawn_new = balance_load()

        time.sleep(POLL_INTERVAL)

        if monitor_interval > 0:
            monitor_cnt += POLL_INTERVAL
            if monitor_cnt > monitor_interval:
                monitor_cnt = 0.0
                for domid in ins_map.keys():
                    ins_map[domid].get_load(print_msg=True)


if __name__ == '__main__':

    if os.geteuid() != 0:
        print("Must have root privileges to run this script")
        sys.exit(1)

    # Assume MWROOT is parent of CWD
    mwroot_default = '/'.join(os.getcwd().split('/')[:-1])

    #
    # Command line arguments
    #

    parser = argparse.ArgumentParser()

    parser.add_argument(
        '-p',
        action='store',
        dest='mwroot_path',
        type=str,
        default=mwroot_default,
        help='Full path to magicwand-commsbackbone files (default: %(default)s)')

    parser.add_argument(
        '-l',
        action='store',
        dest='max_ins_load',
        type=int,
        default=MAX_INS_LOAD_DEFAULT,
        help='INS load (1-100)%% to trigger load balancing (default: %(default)d)')

    parser.add_argument(
        '-m',
        action='store',
        dest='monitor_period',
        type=int,
        default=MONITOR_PERIOD_DEFAULT,
        help='INS load monitor frequency in seconds 0 = disabled (default: %(default)d)')

    parser.add_argument(
        '-g',
        action='store',
        dest='log_level',
        type=str,
        choices=['critical', 'error', 'warning', 'info', 'debug'],
        default='info',
        help='Logging level (default: %(default)s)')

    parser.add_argument(
        '-o',
        action='store',
        dest='overloaded_trigger',
        type=str,
        default=None,
        help='Full path to INS overloaded trigger file (default: %(default)s)')

    parser.add_argument(
        '-s',
        action='store_true',
        dest='start_all_ins_instances',
        default=False,
        help='Start all INS instances immediately instead of on demand (default: %(default)s)')

    parser.add_argument(
        '-i',
        action='store',
        dest='ins_instance_limit',
        type=int,
        default=0,
        help='Limit number of INS instances (default: %(default)d == do not limit)')

    args = parser.parse_args()

    # Sanity check command line arguments

    if args.max_ins_load < 1 or args.max_ins_load > 100:
        parser.error("MAX_INS_LOAD value must be 1 - 100 inclusive")

    if not os.path.isdir(args.mwroot_path):
        parser.error("MWROOT_PATH is not a directory")

    if args.monitor_period < 0 or args.monitor_period > 3600:
        parser.error("MONITOR_PERIOD value must be 0 - 3600 inclusive")

    if args.overloaded_trigger and os.path.isfile(args.overloaded_trigger):
        parser.error("OVERLOADED_TRIGGER file should not exist")

    if args.ins_instance_limit < 0:
        parser.error("INS_INSTANCE_LIMIT should be 0 or greater")

    mwroot_current = args.mwroot_path
    max_ins_load_percent = args.max_ins_load / 100.0
    monitor_interval = args.monitor_period
    ins_overloaded_trigger = args.overloaded_trigger
    start_all_ins_instances = args.start_all_ins_instances
    ins_instance_limit = args.ins_instance_limit

    # Obtain shared mwcomms/ins values (common/common_config.h)

    common_config_h = '{0}/{1}'.format(mwroot_current, path_common_config_h)

    with open(common_config_h, "r") as f:
        searchlines = f.readlines()
    for i, line in enumerate(searchlines):
        if "MAX_INS_COUNT" in line:
            max_ins_count = int(line.split()[2])
        if "HEARTBEAT_INTERVAL_SEC" in line:
            heartbeat_interval_sec_v = int(line.split()[2])
        if "HEARTBEAT_MAX_MISSES" in line:
            heartbeat_max_misses_v = int(line.split()[2])

    if max_ins_count == 0:
        print("Shared common define max_ins_count == 0")
        sys.exit(1)
    if heartbeat_interval_sec_v == 0:
        print("Shared common define heartbeat_interval_sec == 0")
        sys.exit(1)
    if heartbeat_max_misses_v == 0:
        print("Shared common define heartbeat_max_misses == 0")
        sys.exit(1)
    if ins_instance_limit > max_ins_count:
        print("INS instance limit cannot be greater than max_ins_count {0}".format(max_ins_count))
        sys.exit(1)

    # Obtain GIT branch name, required to find RUMP files
    p = subprocess.Popen(['git', 'symbolic-ref', '--short', 'HEAD'],
                         stdout = subprocess.PIPE, stderr = subprocess.PIPE)

    (stdout, stderr) = p.communicate()

    rc = p.wait()
    if rc:
        raise RuntimeError("Call to git failed: {0}".format(stderr))

    mwroot_branch = '-' + stdout.rstrip('\r\n')

    if mwroot_branch == "-master" or mwroot_branch == "-HEAD":
        mwroot_branch = ''

    # Limit maximum INS instances if requested
    if ins_instance_limit != 0 and ins_instance_limit < max_ins_count:
        max_ins_count_v = ins_instance_limit
    else:
        max_ins_count_v = max_ins_count

    #
    # Check prerequisites
    #

    # IPv4 port forwarding
    route_str = subprocess.check_output(['/sbin/route'])
    match = re.search(r'\ndefault.*\n', route_str)
    iface = match.group().strip('\n').split()[7]

    forward_enabled = False

    forward_file_1 = '/proc/sys/net/ipv4/ip_forward'
    forward_file_2 = '/proc/sys/net/ipv4/conf/' + iface + '/forwarding'

    try:
        with open(forward_file_1, 'r') as f1:
            if '1' in f1.read():
                forward_enabled = True

        with open(forward_file_2, 'r') as f2:
            if '1' in f2.read():
                forward_enabled = True
    except:
       pass

    if not forward_enabled:
        print("ipv4 forwarding not enabled, configuration files checked:")
        print(forward_file_1)
        print(forward_file_2)
        sys.exit(1)

    logging.basicConfig(format='%(levelname)s: %(message)s',
        level=log_levels[args.log_level])

    logging.debug("PID = {0}".format(os.getpid()))
    logging.debug("mwroot_current = {0}".format(mwroot_current))
    logging.debug("max_ins_count = {0}".format(max_ins_count))
    logging.debug("max_ins_count_v = {0}".format(max_ins_count_v))
    logging.debug("ins_instance_limit = {0}".format(ins_instance_limit))
    logging.debug("heartbeat_interval_sec_v = {0}".format(heartbeat_interval_sec_v))
    logging.debug("heartbeat_max_misses_v = {0}".format(heartbeat_max_misses_v))
    logging.debug("monitor_interval = {0}".format(monitor_interval))
    logging.debug("max_ins_load_percent = {0}".format(max_ins_load_percent))
    logging.debug("ins_overloaded_trigger = {0}".format(ins_overloaded_trigger))
    logging.debug("start_all_ins_instances = {0}".format(start_all_ins_instances))

    signal.signal(signal.SIGINT,  handler)
    signal.signal(signal.SIGTERM, handler)
    signal.signal(signal.SIGABRT, handler)
    signal.signal(signal.SIGQUIT, handler)

    ins_runner()

    # Dereference all items (destroys INS instances and delete associated iptables entries)
    ins_map = None

    logging.info("Exiting main thread")

