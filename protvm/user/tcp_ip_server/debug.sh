#!/bin/sh

echo ""
echo "Debugging ld-preloaded tcp/ip app ..."
echo ""

echo set environment LD_PRELOAD=$PWD/tcp_ip_wrapper.so > gdbcommands

sudo gdb --command=gdbcommands ./test_server

rm gdbcommands
