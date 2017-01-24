#!/bin/sh

echo ""
echo "Debugging ld-preloaded tcp/ip app ..."
echo ""

echo set environment LD_PRELOAD=$PWD/../wrapper/tcp_ip_wrapper.so > gdbcommands
echo set verbose on >> gdbcommands
echo dir /home/alex/magicwand-commsbackbone/common >> gdbcommands

sudo gdb -tui --command=gdbcommands ./test_client

rm gdbcommands
