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
##   This script is run in an environment that is configured for Rump
##   (i.e. rumprun is in the PATH; one way to achieve this is to
##         source in RUMP_ENV.sh)
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

MW_XENSTORE_ROOT = b"/mw"

INS_MEMORY_MB = 256
POLL_INTERVAL = 0.01
DEFAULT_TIMEOUT = 2

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
## Constants for RUMP/NetBSD socket and TCP options that we may need
## 


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

"""

# Tuple: ( ASCII name, numeric value, type, valid range )
# type: boolean or integer

sock_opts = [
    ( "SO_SNDBUF", 0x1001, int, (0x50, 0x4000) ),
    ( "SO_RCVBUF", 0x1002, int, (0x50, 0x4000) ),

    ( "SO_SNDTIMEO", 0x100b, float, (1, 5) ),
    ( "SO_RCVTIMEO", 0x100b, float, (1, 5)  ),
]


def generate_net_opts():
    """ Randomly picks network options, returns them in string. """
    params = list()
    for (name, numname, t, r ) in sock_opts:
        if int == t:
            val = random.randint( r[0], r[1] )
            params.append( "{0}:{1}:{2}".format( name, numname, val ) )
        elif float == t:
            val = random.uniform( r[0], r[1] )
            params.append( "{0}:{1}:{2:0.03f}".format( name, numname, val ) )
    return " ".join( params )


# Generic storage for INS
class INS:
    def __init__( self, domid ):
        self.domid        = domid
        self.ip           = None
        self.stats        = None
        self.last_contact = time.time()

    def __str__( self ):
        return ("id {0} IP {1} stats {2} contact {3}".
                format( self.domid, self.ip, self.stats, self.last_contact ) )


# Map: domid => INS instance
ins_map = dict()

# MACs for usage by INSs; this way we don't overflow DHCP's mappings
macs = { '00:16:3e:28:2a:58' : { 'in_use' : False },
         '00:16:3e:28:2a:59' : { 'in_use' : False },
         '00:16:3e:28:2a:5a' : { 'in_use' : False },
         '00:16:3e:28:2a:5b' : { 'in_use' : False },
         '00:16:3e:28:2a:5c' : { 'in_use' : False },
         '00:16:3e:28:2a:5d' : { 'in_use' : False } }

inst_num = 0


exit_requested = False
def handler(signum, frame):
    global exit_requested
    print( "Caught signal {0}".format( signum ) )
    exit_requested = True


class PortForwarder:
    """
    Class that manages iptables rules for TCP traffic forwarding to
    support MagicWand's management of multiple INSs. Each instance of
    this class forwards one port from the Dom0 to the same port on an
    INS.

    NOTE:
    These rules necessitate that incoming connections originate from 
    somewhere other than the Dom0 (or associated DomU's ???).

    These two rules are needed to forward port 2200 to IP (INS) 1.2.3.4:
    1. iptables -A FORWARD -p tcp --dport 2200 -j ACCEPT 

    2. iptables -t nat -I PREROUTING -m tcp -p tcp --dport 2200 \
        -j DNAT --to-destination 1.2.3.4
    """

    def __init__( self, in_port, dest_ip ):
        # List of tuples: (chain, rule). The first is always baseline
        # rule to accept established traffic
        self._rules = list()
        self._port = in_port
        self._dest = dest_ip

        self._redirect_conn_to_addr()

    def __del__( self ):
        #print "Stop redirect: 0.0.0.0:{0} ==> {1}:{0}".format( self._port, self._dest )
        for (c,r) in self._rules:
            c.delete_rule( r )

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
            ins_map[ domid ] = INS( domid )
        elif 'ip_addrs' in path:
            ips = filter( lambda s: '127.0.0.1' not in s, newval.split() )
            assert len(ips) == 1, "too many public IP addresses"
            ins_map[ domid ].ip = ips[0]
            client[ b"/mw/{0}/sockopts".format(domid) ] = generate_net_opts()
        elif 'network_stats' in path:
            ins_map[ domid ].stats = [ int(x,16) for x in newval.split(':') ]
        elif 'heartbeat' in path:
            ins_map[ domid ].last_contact = time.time()
        elif 'listening_ports' in path:
            ports = [ int(p, 16) for p in newval.split() ]
            ip = ins_map[ domid ].ip

            # Rebuild the forwarding rules: get rid of old ones and re-create
            self._forwarders = list() # releases references to the old rules
            print( "Redirections:\n------------------" )
            for p in ports:
                f = PortForwarder( p, ip )
                print( "    {0}".format( f ) )
                self._forwarders.append( f )
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


class Ins:
    def __init__( self, xenstore_watcher ):
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

        self._watcher = xenstore_watcher

        ins_run_file = os.environ[ 'XD3_INS_RUN_FILE' ]

        print("PATH: {0}".format( os.environ['PATH'] ) )

        # Find available MAC
        mac = random.sample( [ k for k,v in macs.items() if not v['in_use'] ], 1 )
        #mac = random.sample( filter( lambda k,v: v['in_use'], macs.items() ) )
        if not mac:
            raise RuntimeError( "No more MACs are available" )
        mac = mac[0]
        macs[ mac ]['in_use'] = True

        #mac = None
        #for m in macs.iterkeys():
        #    if macs[m]['in_use']:
        #        continue
        #    macs[m]['in_use'] = True
        #    mac = m
        #    break
        #    if not mac:

        self._mac = mac

        # We will not connect to the console (no "-i") so we won't wait for exit below.
        cmd  = 'rumprun -S xen -d -M {0} '.format( INS_MEMORY_MB )
        cmd += '-N mw-ins-rump-{0:04x} '.format( inst_num )
        cmd += '-I xen0,xenif,mac={0} -W xen0,inet,dhcp {1}'.format( mac, ins_run_file )

        inst_num += 1

        print( "Running command {0}".format(cmd) )

        p = subprocess.Popen( cmd.split(),
                              stdout = subprocess.PIPE, stderr = subprocess.PIPE )
        #(stdout, stderr ) = p.communicate()

        #rc = p.wait()
        t = 0
        while True:
            rc = p.poll()
            if exit_requested:
                break
            if rc is None:
                time.sleep( POLL_INTERVAL )
                t += POLL_INTERVAL
            elif t >= DEFAULT_TIMEOUT:
                raise RuntimeError( "Call to xl took too long: {0}\n".format( p.stderr.read() ) )
            break

        if p.returncode:
            raise RuntimeError( "Call to xl failed: {0}\n".format( p.stderr.read() ) )

        self._domid = int( p.stdout.read().split(':')[1] )

    def __del__( self ):
        """ Destroy the INS associated with this object. """

        print( "Destroying INS {0}".format( self._domid ) )
        p = subprocess.Popen( ["xl", "destroy", "{0}".format(self._domid) ],
                              stdout = subprocess.PIPE, stderr = subprocess.PIPE )
        (stdout, stderr ) = p.communicate()
        rc = p.wait()
        if rc:
            raise RuntimeError( "Call to xl failed: {0}\n".format( stderr ) )

        macs[ self._mac ][ 'in_use' ] = True

    def wait( self ):
        """ Waits until the INS is ready to use """

        t = 0.0
        # The watcher must have populated the INS's IP
        while True:
            if ( self._domid in ins_map and
                 ins_map[ self._domid ].ip ):
                print( "INS {0} is ready with IP {1}".
                       format( self._domid, ins_map[ self._domid ].ip ) )
                break
            time.sleep( POLL_INTERVAL )
            t += POLL_INTERVAL
            if t > DEFAULT_TIMEOUT:
                raise RuntimeError( "wait() is taking too long" )

    def get_domid( self ):
        return self._domid


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
         just call out tothe xl program.
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

def single_ins():
    x = XenIface()
    e = XenStoreEventHandler( x )
    w = XenStoreWatch( e )
    w.start()
    
    s = Ins( w )
    s.wait() # Wait for INS IP address to appear

    while w.is_alive():
        w.join( POLL_INTERVAL )



if __name__ == '__main__':
    print( "Running in PID {0}".format( os.getpid() ) )
    signal.signal( signal.SIGINT,  handler )
    signal.signal( signal.SIGTERM, handler )
    signal.signal( signal.SIGABRT, handler )
    signal.signal( signal.SIGQUIT, handler )

    single_ins()

    print( "Exiting main thread" )
