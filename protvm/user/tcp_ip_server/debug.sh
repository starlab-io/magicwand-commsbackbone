#!/bin/sh

echo ""
echo "Debugging ld-preloaded tcp/ip app ..."
echo ""

echo set environment LD_PRELOAD=$PWD/tcp_ip_wrapper.so > gdbcommands
echo set verbose on >> gdbcommands

sudo gdb -tui --command=gdbcommands ./test_server

rm gdbcommands
