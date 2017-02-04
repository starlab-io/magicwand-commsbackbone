#!/usr/bin/env python
import socket
import sys


if __name__ == '__main__':

    port = int( sys.argv[1] )

    print "Listening on port {0}".format( port )

    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM) #create socket
    s.bind( ("0.0.0.0", port) )   #binding port
    s.listen(1) #listening for only one client

    conn,addr=s.accept()    #accept the connection

    print "Accepted connection: {0}\n".format(addr)

    while True:
        data = conn.recv(2048)
        #import pdb;pdb.set_trace()
        if data == '':
            sys.exit()

        response = data.replace('\x00', '')

        print "Sending back message: '{0}'".format( response )
        conn.send( response )
