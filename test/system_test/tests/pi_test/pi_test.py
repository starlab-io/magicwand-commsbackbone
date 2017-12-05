#!/usr/bin/python3

import socket
import threading
import os.path
import struct
import sys

s = socket.socket()

if len(sys.argv) > 1:
    address = sys.argv[1]
else:
    address = "10.30.30.174" #{0}".format(input("IP Address: 10.30.30."))

if len(sys.argv) > 2:
    port = int(sys.argv[2])
else:
    port = 1024 # int(input("Port: "))

print(address)
print(port)

s.connect((address, port))

f = open("{0}/pi.txt".format(os.path.expanduser("~")))

# b = 2000
b = 50000

txt = f.read(b)
f.close()

s.send(txt.encode("utf-8"))

txtlen = 0

while (True):
    txt = s.recv(1024)
    if str(txt) == "b''":
        print("Connection closed by server")
        del(s)
        sys.exit(104)
    try:
        print(str(txt.decode()))
    except:
        print(str(txt))
    txtlen = txtlen + len(txt)
    if txtlen >= b:
        print("Test complete.")
        del(s)
        quit()
