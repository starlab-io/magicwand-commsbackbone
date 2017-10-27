#!/bin/sh

echo ""
echo "Debugging ld-preloaded tcp/ip app ..."
echo ""

echo set environment LD_PRELOAD=$PWD/../wrapper/tcp_ip_wrapper.so > gdbcommands
echo set verbose on >> gdbcommands
#echo dir /home/alex/magicwand-commsbackbone/common >> gdbcommands

sudo gdb --command=gdbcommands --args ./multi_client_server 6543

rm gdbcommands
