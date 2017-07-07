#!/usr/bin/env python2

##
## Front-end glue for MagicWant that spawns Rump INSs and routes
## incoming connections to a specific instance based on runtime
## variables.
##
## Package requirements:
##   python-iptables (pip3 install --upgrade python-iptables)
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

XEND_PORT = 9225 # the port configured for xend's xen-api-server value
MW_XENSTORE_ROOT = b"/mw"

INS_MEMORY_MB = 256
#EXT_IFACE = "eth0" # external-facing network interface
EXT_IFACE = "xenbr0" # external-facing network interface


inst_num = 0

import sys
import os
import time
import re
import subprocess
import httplib
import xmlrpclib
import socket
import thread
import threading
import iptc # iptables bindings

# Pull in the Xen API - find a better way to do this...
sys.path.append( '/usr/lib/xen-4.4/lib/python' )
import xen
import xen.xm.XenAPI as XenAPI

import pyxs

##import libvirt # system-installed build doesn't support new XenAPI

# See http://libvirt.org/docs/libvirt-appdev-guide-python
#import libvirt # This keeps us on Python 2 for now.

class ConnectionRouter:
    """
    Class that manages iptables rules for TCP traffic forwarding to
    support MagicWand's management of multiple INSs. Only one forwarding 
    rule is enabled at a time.

    These two rules are needed to forward port 2200 to IP (INS) 1.2.3.4:
    1. iptables -A FORWARD -p tcp --dport 2200 -j ACCEPT 

    2. iptables -t nat -I PREROUTING -m tcp -p tcp --dport 2200 \
        -j DNAT --to-destination 1.2.3.4

    """
    def __init__( self ):
        # List of tuples: (chain, rule). The first is always baseline
        # rule to accept established traffic
        self._rules = list()

        # See https://gist.github.com/apparentlymart/d8ebc6e96c42ce14f64b
        # for iptable inspiration

        #  iptables -A INPUT -m state --state RELATED,ESTABLISHED -j ACCEPT
        chain = iptc.Chain( iptc.Table(iptc.Table.FILTER), "INPUT" )

        rule = iptc.Rule() # accept establish
        rule.target = iptc.Target( rule, "ACCEPT" )

        match = iptc.Match( rule, "state" )
        match.state = "RELATED,ESTABLISHED"
        rule.add_match( match )

        chain.insert_rule( rule )
        self._rules.append( (chain, rule) )

    def __del__( self ):
        for (c,r) in self._rules:
            c.delete_rule( r )

    def _insert_routing_rule( self, chain, rule ):
        chain.insert_rule( rule )

        # Remove the previous prerouting rule
        if len(self._rules) > 1:
            self._rules[1][0].delete_rule( self._rules[1][1] )
            del self._rules[1]
        # Track the new rule internally
        self._rules.append( (chain, rule) )

    def redirect_conn_to_port( self, orig_dport, new_dport ):

        # iptables -t nat -A PREROUTING -p tcp --dport 9000 -j REDIRECT --to-port 9001
        rule  = iptc.Rule()
        rule.protocol = "tcp"
        
        match = iptc.Match( rule, "tcp" )
        match.dport  = "{0:d}".format( orig_dport )

        rule.target = iptc.Target( rule, "REDIRECT" )
        rule.add_match( match )
        rule.target.to_ports = "{0:d}".format( new_dport )

        chain = iptc.Chain( iptc.Table( iptc.Table.NAT ), "PREROUTING" )

        self._insert_routing_rule( chain, rule )

    def redirect_conn_to_iface( self, orig_dport, dest_iface ):
        # iptables -t nat -A PREROUTING -p tcp --dport 9000 -j REDIRECT --to-port 9001
        # iptables -A FORWARD -i eth0 -o eth1 -p tcp --dport 80  -j ACCEPT

        rule  = iptc.Rule()
        rule.protocol = "tcp"
        #rule.target = iptc.Target( rule, "REDIRECT" )
        rule.target = iptc.Target( rule, "ACCEPT" )
        rule.in_interface = EXT_IFACE
        rule.out_interface = dest_iface

        match = iptc.Match( rule, "tcp" )
        match.dport  = "{0:d}".format( orig_dport )
        rule.add_match( match )

        #chain = iptc.Chain( iptc.Table( iptc.Table.NAT ), "PREROUTING" )
        #chain = iptc.Chain( iptc.Table( iptc.Table.FILTER ), "FORWARD" )
        chain = iptc.Chain( iptc.Table( iptc.Table.FILTER ), "INPUT" )

        self._insert_routing_rule( chain, rule )

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
    def __init__( self, conn ):
        self._conn = conn

    def event( self, path, newval ):
        s_newval = newval
        if not newval:
            s_newval = "<deleted>"
        print( "{0} => {1}".format( path, s_newval ) )


class XenStoreWatch( threading.Thread ):
    def __init__( self, event_handler ):
        threading.Thread.__init__( self )
        self._handler = event_handler\

    def handle_xs_change( self, path, newval ):
        pass

    def run( self ):
        with pyxs.Client() as c:
            m = c.monitor()
            m.watch( MW_XENSTORE_ROOT, b"MW INS watcher" )
            events = m.wait()

            for e in events: # blocking is here on generator
                path = e[0]
                value = None
                if c.exists( path ):
                    value = c[path]

                self._handler.event( path, value )

def spawn_ins():
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

    print("PATH: {0}".format( os.environ['PATH'] ) )

    # We will not connect to the console (no "-i") so we won't wait for exit below.
    cmd  = 'rumprun -S xen -d -M {0} '.format( INS_MEMORY_MB )
    cmd += '-N mw-ins-rump-{0:x} '.format( inst_num )
    cmd += '-I xen0,xenif -W xen0,inet,dhcp {0}'.format( ins_run_file )

    inst_num += 1

    print( "Running command {0}".format(cmd) )

    p = subprocess.Popen( cmd.split(),
                          stdout = subprocess.PIPE, stderr = subprocess.PIPE )
    (stdout, stderr ) = p.communicate()
    rc = p.wait()
    if rc:
        raise RuntimeError( "Call to xl failed: {0}\n".format( stderr ) )


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
        # Xen 4.4 style connection
        x = XenAPI.Session( 'http://localhost:9225' )

        # 4.4, but emulate xapi_local()
        #x = XenAPI.Session( "http://_var_run_xenstored_socket", transport=UDSTransport() )
    
        # More recent Xen connection
        #x = XenAPI.xapi_local() # method does not exist

        x.xenapi.login_with_password("root", "")

        self._conn = x

    def __del__( self ):
        self._conn.logout()

    def get_conn( self ):
        return self._conn

    def get_all_vms( self ):
        return self._conn.xenapi.VM.get_all_records()

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
    r = ConnectionRouter()
    r.redirect_conn_to_iface( 2200, 'vif23.0' )

    r.dump()
    while True:
        time.sleep(5)

def test_xen_stuff():
    x = XenIface()

    e = XenStoreEventHandler( x )

    w = XenStoreWatch( e )
    w.start()

    spawn_ins()

if __name__ == '__main__':
    print( "Running in PID {0}".format( os.getpid() ) )
    test_redir()
    #test_xen_stuff()

    print( "Exiting main thread" )

    '''
    while True:
        vms = x.get_all_vms()
    
        for (k,v) in vms.iteritems():
            # Skip Dom0
            domid = int(v['domid'])
            if domid == 0:
                continue

            net = x.get_vif( domid )
            #print "{0:d} {1:s} {2}".format( domid, v['name_description'], net['iface'] )

        time.sleep( 2 )
    '''
