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
##   (i.e. RUMP_ENV.sh has been sourced in
##
##   xend is installed and accessible via TCP on localhost
##      On Ubuntu 14.04 / Xen 4.4, change /etc/xen/xend-config.sxp so
##      this directive is enabled:
##           (xen-api-server ((9225 localhost)))
##
##

XEND_PORT = 9225 # the port configured for xend's xen-api-server value

import sys
import os
import time
import re
import subprocess
import httplib
import xmlrpclib
import socket
import iptc # iptables bindings

# Pull in the Xen API - find a better way to do this...
sys.path.append( '/usr/lib/xen-4.4/lib/python' )
import xen
import xen.xm.XenAPI as XenAPI


# See http://libvirt.org/docs/libvirt-appdev-guide-python
#import libvirt # This keeps us on Python 2 for now.

class ConnectionRouter:
    """
    Class that manages iptables rules for TCP traffic forwarding to
    support MagicWand's management of multiple INSs. Only one forwarding 
    rule is enabled at a time.
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

    def send_new_conn_to_port( self, orig_dport, new_dport ):

        # iptables -t nat -A PREROUTING -p tcp --dport 9000 -j REDIRECT --to-port 9001
        chain = iptc.Chain( iptc.Table( iptc.Table.NAT ), "PREROUTING" )
        rule  = iptc.Rule()
        rule.protocol = "tcp"
        
        match = iptc.Match( rule, "tcp" )
        match.dport  = "{0:d}".format( orig_dport )

        rule.target = iptc.Target( rule, "REDIRECT" )
        rule.add_match( match )
        rule.target.to_ports = "{0:d}".format( new_dport )

        chain.insert_rule( rule )

        # Remove the previous prerouting rule
        if len(self._rules) > 1:
            self._rules[1][0].delete_rule( self._rules[1][1] )
            del self._rules[1]
        # Track the new rule internally
        self._rules.append( (chain, rule) )

    def send_new_conn_to_iface( self, inport, dest_iface ):
        # iptables -t nat -A PREROUTING -p tcp --dport 9000 -j REDIRECT --to-port 9001
        chain = iptc.Chain( iptc.Table( iptc.Table.NAT ), "PREROUTING" )
        rule  = iptc.Rule()
        rule.protocol = "tcp"
        raise NotImplementedError # implement me!!!
        #rule.out_interface = ""

    def dump( self ):
        table = iptc.Table(iptc.Table.FILTER)
        for table in ( iptc.Table.FILTER, iptc.Table.NAT, 
                       iptc.Table.MANGLE, iptc.Table.RAW ):
            iptc_tbl = iptc.Table( table )
            print "Table", iptc_tbl.name
            for chain in iptc_tbl.chains:
                if not chain.rules:
                    continue
                print "======================="
                print "Chain ", chain.name
                for rule in chain.rules:
                    print "Rule", "proto:", rule.protocol, "src:", rule.src, "dst:", \
                        rule.dst, "in:", rule.in_interface, "out:", rule.out_interface,
                    print "Matches:",
                    for match in rule.matches:
                        print match.name,
                        print "Target:",
                        print rule.target.name
                print "======================="


class Domain:
    def __init__( self, conn, domid ):
        self._conn = conn
        self._domid = domid


class XenStore:
    def __init__( self ):
        pass


def spawn_ins():
    pass


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

def xend_connect():
    """
    Connects to xend XML-RPC server and returns connection object, which
    caller must close.
    """

    # Xen 4.4 style connection
    x = XenAPI.Session( 'http://localhost:9225' )

    # 4.4, but emulate xapi_local()
    #x = XenAPI.Session( "http://_var_run_xenstored_socket", transport=UDSTransport() )
    
    # More recent Xen connection
    #x = XenAPI.xapi_local() # method does not exist

    x.xenapi.login_with_password("root", "")
    
    return x

def print_dict(desc, mydict):
    print desc
    for (k,v) in mydict.iteritems():
        print k
        print v
    print '----------'

if __name__ == '__main__':
    x = xend_connect()
    
    try:
        vms = x.xenapi.VM.get_all_records()
        print_dict( "VMs", vms )

        vifs = x.xenapi.VIF.get_all_records()
        print_dict( "vifs", vifs )

        nets = x.xenapi.network.get_all()
        print "nets", nets
        for net in nets:
            name = x.xenapi.network.get_name_label( net )
            r    = x.xenapi.network.get_record( net )
            print name, r
        print "----------------"

        #tunnels = x.xenapi.tunnel.get_all()
        #print "tunnels", tunnels
        #print_dict( "tunnels", tunnels )

        # VLAN: get_all() and get_all_records() both unsupported
        #vlans = x.xenapi.VLAN.get_all_records()
        #print "VLAN", vlans
        ##print_dict("VLAN", vlans )

        # PIF
        pifs = x.xenapi.PIF.get_all_records()
        print_dict( "PIFs", pifs )

        # VBD
        vbds = x.xenapi.VBD.get_all_records()
        print_dict( "VBDs", vbds )

        # PCI
        #pcis = x.xenapi.PCI.get_all_records()
        #print_dict( "PCIs", pcis )

        for (k,v) in vms.iteritems():
            #r = x.xenapi.VM.get_record( v )
            if x.xenapi.VM.get_is_control_domain(k): ##0" == r['domid']:
                continue
            #print k,v
            #vs = x.xenapi.VM.get_VIFs( k )
            #print "vifs:", vs
            vifs = [ x.xenapi.VIF.get_record(v) for vif in vifs ]


            print "{0:s} {1:s}".format( v['domid'], v['name_description'])
            #print vifs
    finally:
        x.logout()
