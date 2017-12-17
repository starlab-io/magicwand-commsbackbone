#!/usr/bin/env python2

##
## Front-end glue for MagicWand that spawns Rump INSs and routes
## incoming connections to a specific instance based on runtime
## variables.
##
## Package requirements:
##   python-iptables: pip install --upgrade python-iptables
##   pysx           : pip install --upgrade pyxs
##
## Environmental requirements:
##
##   This script is run as root in an environment that is configured
##   for Rump, i.e. rumprun is in the PATH; one way to achieve this is
##   to source in RUMP_ENV.sh)
##
##   xend is installed and accessible via TCP on localhost
##      On Ubuntu 14.04 / Xen 4.4, change /etc/xen/xend-config.sxp so
##      this directive is enabled:
##           (xen-api-server ((9225 localhost)))
##      Install it as a service with:
##       $ ln -s /usr/lib/xen-4.4/bin/xend /etc/init.d
##       $ update-rc.d xend defaults 20 21
##      and start/restart the service
##       $ service xend start
##
## Other notes:
##
##   The XenStore event watcher can block forever if the PVM driver
##   isn't loaded or in the right state. See
##   https://bugs.python.org/issue8844 and the pyxs source (client.py)
##   for details.
##
##   XenAPI is broken on Ubuntu 14.04/Xen 4.4, so we don't use it. We 
##   interface with Xen by calling out to xl.
##
##   The INS memory requirement is based on the number of sockets it
##   supports. Take this into account here (manually or
##   programmatically).
##

MW_XENSTORE_ROOT = b"/mw"

# ToDo: INS will launch "successfully" with 256MB, but will not publish heartbeat/network_stats to xenstore, leading the PVM to think it's dead. Giving it 3GB allows the INS to function fully. Why is that?
INS_MEMORY_MB = 1024
POLL_INTERVAL = 0.05
DEFAULT_TIMEOUT = 5

# ToDo: Create a dependency on common/common_config.h to supply these values
HEARTBEAT_INTERVAL_SEC = 15
HEARTBEAT_MAX_MISSES = 2
MAX_INS_COUNT = 2

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

# Broken on Ubuntu 14.04
# import xen.xm.XenAPI as XenAPI

# See http://libvirt.org/docs/libvirt-appdev-guide-python
#import libvirt # system-installed build doesn't support new XenAPI


##
## Constants for RUMP/NetBSD socket and TCP options that we may need.
## 


# Per-socket options: use these for mitigation

"""
include/sys/socket.h


/*
 * Additional options, not kept in so_options.
 */
#define SO_SNDBUF	0x1001		/* send buffer size */
#define SO_RCVBUF	0x1002		/* receive buffer size */
#define SO_SNDLOWAT	0x1003		/* send low-water mark */
#define SO_RCVLOWAT	0x1004		/* receive low-water mark */
/* SO_OSNDTIMEO		0x1005 */
/* SO_ORCVTIMEO		0x1006 */
#define	SO_ERROR	0x1007		/* get error status and clear */
#define	SO_TYPE		0x1008		/* get socket type */
#define	SO_OVERFLOWED	0x1009		/* datagrams: return packets dropped */

#define	SO_NOHEADER	0x100a		/* user supplies no header to kernel;
					 * kernel removes header and supplies
					 * payload
					 */
#define SO_SNDTIMEO	0x100b		/* send timeout */
#define SO_RCVTIMEO	0x100c		/* receive timeout */


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




# Map: domid => INS instance
ins_map = dict()

# List of INS's that do not have domids assigned
ins_queue = list()

# MACs for usage by INSs; this way we don't overflow DHCP's mappings
macs = { '00:16:3e:28:2a:50' : { 'in_use' : False },
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
def handler(signum, frame):
    global exit_requested
    logging.warn( "Caught signal {0}".format( signum ) )
    exit_requested = True


def generate_sys_net_opts():
    """ Randomly, but smartly generate INS network configuration """
    params = list()

    # Calc send, recv settings
    for prefix in ("send", "recv"):
        bufauto = random.randint( 0, 1)
        params.append( "{0}buf_auto:{1}".format( prefix, bufauto ) )

        #if bufauto:
        #    continue

        # initial buffer size
        #bufspace = random.randrange( 0x1000, 0x40001, 0x1000 )
        bufspace = random.choice( xrange( 0x1000, 0x40001, 0x1000 ) )
        params.append( "{0}space:{1:x}".format( prefix, bufspace ) )

        # buffer increment size
        #bufinc = random.randrange( bufspace / 4, bufspace / 2, 0x800 )
        bufinc = random.choice( xrange( bufspace/4, bufspace/2, 0x800 ) )
        params.append( "{0}buf_inc:{1:x}".format( prefix, bufinc ) )

        # max buffer size
        #bufmax = random.randrange( bufspace, bufspace * 4, 0x1000 )
        bufmax = random.choice( xrange( bufspace, bufspace * 4, 0x1000 ) )
        params.append( "{0}buf_max:{1:x}".format( prefix, bufmax ) )

        assert bufmax >= bufspace, "nonsensical space vs max values"

    # Calc other settings
    params.append( "init_win:{0:x}".format( random.randint( 2, 6 ) ) )
    params.append( "init_win_local:{0:x}".format( random.randint( 2, 6) ) )
    params.append( "delack_ticks:{0:x}".format( random.randint( 10, 40) ) )
    params.append( "congctl:{0}".format( random.choice( ["reno", "newreno", "cubic"] ) ) )

    logging.debug( "TCP/IP parameters:\n\t{0}".format( "\n\t".join( params ) ) )
    opts = " ".join( params )
    logging.debug( "TCP/IP parameter value: {0}".format( opts ) )
    return opts


# Generic storage for INS
class INS:
    def __init__( self, domid=None ):
        self.domid             = domid
        self.ip                = None
        self.stats             = dict()
        self.last_contact      = time.time()
        self.missed_heartbeats = 0
        self._lock             = threading.Lock()
        self._forwarders       = list()
        self._active           = False

        if domid == None:
            self._create()

    def __del__( self ):
        """ Destroy the INS associated with this object. """

        p = subprocess.Popen( ["xl", "destroy", "{0:d}".format(self.domid) ],
                              stdout = subprocess.PIPE, stderr = subprocess.PIPE )
        (stdout, stderr ) = p.communicate()
        rc = p.wait()
        if rc:
            raise RuntimeError( "Call to xl failed: {}\n".format( stderr ) )

        macs[ self._mac ][ 'in_use' ] = False

        logging.info( "Destroyed INS {}".format( self ) )

    def __str__( self ):
        return ("id {} IP {} contact {} {}active".
                format( self.domid, self.ip, self.last_contact,
                        { False : "in", True : "" } [self._active ] ) )

    def __repr__( self ):
        return str( self )

    def _create( self ):
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

        ins_run_file = os.environ[ 'XD3_INS_RUN_FILE' ]

        logging.debug( "PATH: {0}".format( os.environ['PATH'] ) )

        # Find available MAC
        mac = random.sample( [ k for k,v in macs.items() if not v['in_use'] ], 1 )
        if not mac:
            raise RuntimeError( "No more MACs are available" )
        mac = mac[0]
        macs[ mac ]['in_use'] = True
        self._mac = mac

        # We will not connect to the console (no "-i") so we won't wait for exit below.
        cmd  = 'rumprun -S xen -d -M {0} '.format( INS_MEMORY_MB )
        #cmd += '-p -D 1234 ' # DEBUGGING ONLY **********************
        cmd += '-N mw-ins-rump-{0:04x} '.format( inst_num )
        cmd += '-I xen0,xenif,mac={0} -W xen0,inet,dhcp {1}'.format( mac, ins_run_file )

        inst_num += 1

        logging.info( "Running command {0}".format(cmd) )

        p = subprocess.Popen( cmd.split(),
                              stdout = subprocess.PIPE, stderr = subprocess.PIPE )
        #(stdout, stderr ) = p.communicate()

        #rc = p.wait()
        t = 0
        while True:
            rc = p.poll()
            if exit_requested:
                break
            if rc is not None:
                break
            if t >= DEFAULT_TIMEOUT:
                raise RuntimeError( "Call to xl took too long: {0}\n".format( p.stderr.read() ) )
            else:
                time.sleep( POLL_INTERVAL )
                t += POLL_INTERVAL

        if p.returncode:
            raise RuntimeError( "Call to xl failed: {0}\n".format( p.stderr.read() ) )

        # self.domid = int( p.stdout.read().split(':')[1] )
        ins_queue.append(self)

    def set_listening_ports( self, port_list ):
        self.lock()
        try:
            for p in port_list:
                if p in [ x.get_port() for x in self._forwarders ]:
                    continue
                fwd = PortForwarder( p, self.ip )
                self._forwarders.append( fwd )
        finally:
            self.unlock()
    
    def set_active( self, activated ):
        self.lock()
        try:
            for f in self._forwarders:
                f.set_active( activated )
        finally:
            self.unlock()
        self._active = activated
        if activated:
            logging.info( "Active: {}".format( self.domid ) )
        
    def is_active( self ):
        return self._active

    def register_heartbeat( self ):
        self.lock()
        try:
            self.last_contact = time.time()
            self.missed_heartbeats = 0
        finally:
            self.unlock()

    def check_heartbeat( self ):
        """ False ==> this INS is dead """
        alive = True
        self.lock()
        try:
            if( time.time() - self.last_contact >
                HEARTBEAT_INTERVAL_SEC * (self.missed_heartbeats + 1) + 1 ):
                self.missed_hearbeats += 1
                logging.warn( "INS {} has missed {} heartbeat(s)\n".
                              format(self.domid, self.missed_heartbeats) )

                if self.missed_heartbeats >= HEARTBEAT_MAX_MISSES:
                    logging.warn( "INS {} is now considered dead\n".format(self.domid) )
                    alive = False
        finally:
            self.unlock()

        return alive
            
    def lock( self ):
        self._lock.acquire()

    def unlock( self ):
        self._lock.release()

    def update_stats( self, stats_str ):
        self.lock()
        try:
            # See hearbeat_thread_func in xenevent.c for this format
            stats = [ int(x,16) for x in stats_str.split(':') ]
            self.stats[ 'max_sockets' ]  = stats[0]
            self.stats[ 'used_sockets' ] = stats[1]
            self.stats[ 'recv_bytes' ]   = stats[2]
            self.stats[ 'sent_bytes' ]   = stats[3]
        finally:
            self.unlock()

    def get_load( self ):
        used = 1.0 * self.stats.get( 'used_sockets', 0 )
        capacity = 1.0 * self.stats.get( 'max_sockets', 1 )
        return used / capacity

    def overloaded( self ):
        """ At 80%+ of max load """
        return self.get_load() >= 0.80

    def wait( self ):
        """ Waits until the INS is ready to use """

        t = 0.0
        # The watcher must have populated the INS's IP
        while True:
            if ( self.domid != None and
                 self.domid in ins_map and
                 self.ip != None ):
                logging.info( "Ready: INS {}".format(self) )
                logging.info( "All: {}".format(ins_map) )
                break

            time.sleep( POLL_INTERVAL )
            t += POLL_INTERVAL
            if t > DEFAULT_TIMEOUT:
                raise RuntimeError( "wait() is taking too long" )


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

    def __init__( self, in_port, dest_ip ):
        # List of tuples: (chain, rule). The first is always baseline
        # rule to accept established traffic
        self._rules = list()
        self._port = in_port
        self._dest = dest_ip
        self._active = False

    def set_active( self, activate ):
        if self._active == activate:
            print( "Not activating already-active rule: {}".format(self) )
            return

        if activate:
            self._redirect_conn_to_addr()
        else:
            self._deactivate()

        self._active = activate

    def active( self ):
        return self._active
    
    def _deactivate( self ):
        logging.info( "Deactivated: {}".format( self ) )

        for (c,r) in self._rules:
            c.delete_rule( r )

        self._rules = list() # drop old refs

    def __del__( self ):
        self._deactivate()

    def __str__( self ):
        return "0.0.0.0:{0} ==> {1}:{0}".format( self._port, self._dest )

    def get_port( self ):
        return self._port

    def _enable_rule( self, chain, rule ):
        chain.insert_rule( rule )
        self._rules.append( (chain, rule) )

    def _redirect_conn_to_addr( self ):
        """
        Configure an external IP address on the "outside" interface and add iptables rule:
        """

        #print "Start redirect: 0.0.0.0:{0} ==> {1}:{0}".format( self._port, self._dest )

        # Forward port 8080 to Y.Y.Y.Y:8080
        # iptables -t nat -A PREROUTING -p tcp --dport 8080 -j DNAT --to Y.Y.Y.Y:8080

        chain = iptc.Chain( iptc.Table( iptc.Table.NAT ), "PREROUTING" )

        rule  = iptc.Rule()
        rule.protocol = "tcp"

        match = iptc.Match( rule, "tcp" )
        match.dport  = "{0:d}".format( self._port )

        rule.target = iptc.Target( rule, "DNAT" )
        rule.target.set_parameter( "to_destination",  "{0}:{1}".format( self._dest, self._port ) )
        rule.add_match( match )
        self._enable_rule( chain, rule )

        # Accept traffic directed at the prerouting chain:
        # iptables -A FORWARD -p tcp --dport 8080 -j ACCEPT

        chain = iptc.Chain( iptc.Table( iptc.Table.FILTER ), "FORWARD" )

        rule  = iptc.Rule()
        rule.protocol = "tcp"

        match = iptc.Match( rule, "tcp" )
        match.dport  = "{0:d}".format( self._port )

        rule.target = iptc.Target( rule, "ACCEPT" )
        rule.add_match( match )

        self._enable_rule( chain, rule )

        # Prerequisite iptables rule
        # iptables -A FORWARD -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT

        chain = iptc.Chain(iptc.Table(iptc.Table.FILTER), "FORWARD")

        rule = iptc.Rule()
        rule.protocol = "tcp"

        match = rule.create_match("conntrack")
        match.ctstate = "RELATED,ESTABLISHED"

        rule.target = iptc.Target(rule, "ACCEPT")

        self._enable_rule(chain, rule)

        logging.info( "Activated: {}".format( self ) )
        
    def dump( self ):
        table = iptc.Table(iptc.Table.FILTER)
        for table in ( iptc.Table.FILTER, iptc.Table.NAT, 
                       iptc.Table.MANGLE, iptc.Table.RAW ):
            iptc_tbl = iptc.Table( table )

            print( "Table {0}".format( iptc_tbl.name ) )
            for chain in iptc_tbl.chains:
                if not chain.rules:
                    continue
                print( "=======================" )
                print( "Chain {0}".format( chain.name ) )
                for rule in chain.rules:
                    print( "Rule: proto: {0} src {1} dst {2} iface {3} out {4}".
                           format( rule.protocol, rule.src, rule.dst, 
                                   rule.in_interface,rule.out_interface ) )
                    print( "Matches:" )
                    for match in rule.matches:
                        print( "{0} target {1}".format( match.name, rule.target.name ) )
                print( "=======================" )


class XenStoreEventHandler:
    def __init__( self, xiface):
        self._xiface = xiface
        self._forwarders = list()

    def event( self, client, path, newval ):
        # example path:
        # s.split('/')
        # ['', 'mw', '77', 'network_stats']
        #print( "Observing {0} => {1}".format( path, s_newval ) )

        # We should only see the MW root
        assert path.startswith( MW_XENSTORE_ROOT ), "Unexpected path {0}".format(path)

        try:
            domid = int( path.split('/')[2] )
        except:
            # We can't do anything without the domid
            return

        if 'ins_dom_id' in path:
            assert domid == int(newval)
            if len(ins_queue) == 0:
                ins_map[ domid ] = INS( domid )
            else:
                ins_queue[0].lock()
                ins_map[ domid ] = ins_queue.pop(0)
                ins_map[ domid ].domid = domid
                ins_map[ domid ].unlock()
        elif 'ip_addrs' in path:
            ips = filter( lambda s: '127.0.0.1' not in s, newval.split() )
            assert len(ips) == 1, "too many public IP addresses"
            ins_map[ domid ].lock()
            ins_map[ domid ].ip = ips[0]
            ins_map[ domid ].unlock()
            #client[ b"/mw/{0}/sockopts".format(domid) ] = generate_sys_net_opts()
        elif 'network_stats' in path:
            ins_map[ domid ].update_stats( newval )
        elif 'heartbeat' in path:
            ins_map[ domid ].register_heartbeat()
        elif 'listening_ports' in path:
            ports = [ int(p, 16) for p in newval.split() ]
            ins_map[ domid ].set_listening_ports( ports )
        else:
            #print( "Ignoring {0} => {1}".format( path, newval ) )
            pass


class XenStoreWatch( threading.Thread ):
    def __init__( self, event_handler ):
        threading.Thread.__init__( self )
        self._handler = event_handler

    def handle_xs_change( self, path, newval ):
        pass

    def run( self ):
        """
        Run the thread. Can block forever and ignore signals in some cases.
        See Other notes at top of file.
        """
        with pyxs.Client() as c:
            m = c.monitor()
            m.watch( MW_XENSTORE_ROOT, b"MW INS watcher" )

            events = m.wait()
            if exit_requested:
                return

            for e in events: # blocking is here on generator
                if exit_requested:
                    return

                path = e[0]
                value = None
                if c.exists( path ):
                    value = c[path]

                self._handler.event( c, path, value )

class UDSHTTPConnection(httplib.HTTPConnection):
    """HTTPConnection subclass to allow HTTP over Unix domain sockets. """
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
         self._extra_headers += [ (key,value) ]
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
     def __init__( self ):
         """
         The Xen python bindings are very broken on Ubuntu 14.04, so we
         just call out to the xl program.
         """

     def get_vif( self, domid ):
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
         #    print( "Path: {0} => {1}".format(p, c[p]) )

         p = subprocess.Popen( [ 'xl', 'network-list', "{0:d}".format( domid ) ],
                               stdout = subprocess.PIPE, stderr = subprocess.PIPE )

         (stdout, stderr ) = p.communicate()

         rc = p.wait()
         if rc:
             raise RuntimeError( "Call to xl failed: {0}".format( stderr ) )

         # Example stdout
         # Idx BE Mac Addr.         handle state evt-ch   tx-/rx-ring-ref BE-path
         # 0   0  00:00:00:00:00:00 0      4     -1       768/769         /local/domain/0/backend/vif/3/0

         # Extract the info we want, put in dict
         lines = stdout.splitlines()
         assert len(lines) == 2, "Unexpected line count"

         info = lines[1].split()
         bepath = info[7]   # /local/domain/0/backend/vif/3/0
         iface = '.'.join( bepath.split('/')[-3:] ) # vif.3.0

         return dict( idx    = info[0],
                      mac    = info[2],
                      state  = info[4],
                      bepath = bepath,
                      iface  = iface )

def print_dict(desc, mydict):
     print( desc )
     for (k,v) in mydict.iteritems():
         print( k )
         print( v )
     print( '----------' )

def test_redir():
     r = PortForwarder( 4567, '10.30.30.50' )
     #r.dump()

     while True:
         if exit_requested:
             return
         time.sleep( POLL_INTERVAL )


def balance_load():
    """
    Serves as load balancer. Routes new connections to least busy INS
    if the current one is too busy. If all of them are too busy, returns
    True so caller will create a new one.
    """

    assert len(ins_map), "No INS is running at all"

    all_active = [ v for (k,v) in ins_map.iteritems() if v.is_active() ]

    if not all_active:
        # There is no active INS; this can happen on initialization. Pick the first one.
        curr = ins_map.values()[0]
        curr.set_active( True )
    else:
        assert len(all_active) == 1, "Only one INS should be active here"
        curr = all_active[0]

    if not curr.overloaded():
        # Nothing to do here
        return False

    # curr is overloaded: shift burden to the INS with the lowest load    
    logging.debug( "Stats: {}, load: {}, overloaded: {}".
                   format( curr.stats, curr.get_load(), curr.overloaded() ) )
    logging.info( "INS {} is overloaded, looking for another one".format(curr) )

    available = [ v for (k,v) in ins_map.iteritems() if not v.overloaded() ]
    if not available:
        # Nothing is available - the caller needs to create a new INS
        return True

    new = min( available, key=lambda x: x.get_load() )

    logging.info( "{} is over-loaded. Directing traffic to {}"
                  .format( curr, new ) )

    new.set_active( True )   # 2 INSs are active
    curr.set_active( False ) # 1 INS is active
    return False


def single_ins():
    x = XenIface()
    e = XenStoreEventHandler( x )
    w = XenStoreWatch( e )
    w.start()

    s = INS()
    s.wait() # Wait for INS IP address to appear

    while w.is_alive():
        w.join( POLL_INTERVAL )


def ins_runner():
    """ Spawns INSs as needed, destroys as permissible. """

    spawn_new = True

    x = XenIface()
    e = XenStoreEventHandler( x )
    w = XenStoreWatch( e )
    w.start()

    # Create more INSs and monitor the workload here.
    # INS activation is handled by balance_load()
    while ( not exit_requested ):
        if spawn_new and len(ins_map) < MAX_INS_COUNT:
            spawn_new = False
            i = INS()
            i.wait()

        # Fresh query of INSs in each iteration; the set may have
        # changed. N.B. INSs are inserted into ins_map by the XS
        # monitor.
        for domid in ins_map.keys():
            # Check for death
            if not ins_map[domid].check_heartbeat():
                del ins_map[domid]

        spawn_new = balance_load()

        time.sleep( POLL_INTERVAL )

if __name__ == '__main__':
    logging.basicConfig( format='%(levelname)s: %(message)s',
                         level=logging.DEBUG )

    logging.debug( "Running in PID {0}".format( os.getpid() ) )

    signal.signal( signal.SIGINT,  handler )
    signal.signal( signal.SIGTERM, handler )
    signal.signal( signal.SIGABRT, handler )
    signal.signal( signal.SIGQUIT, handler )

    #single_ins()
    ins_runner()

    global ins_map
    ins_map = None # deref all items
    logging.info("Exiting main thread" )

