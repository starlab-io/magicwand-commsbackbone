#!/bin/sh

echo ""
echo "Running ld-preloaded tcp/ip app ..."
echo ""

cp index.html /tmp

sudo LD_PRELOAD=$PWD/../wrapper/tcp_ip_wrapper.so ./server -r /tmp
