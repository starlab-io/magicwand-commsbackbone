#!/bin/sh

echo ""
echo "Running ld-preloaded tcp/ip app ..."
echo ""

sudo LD_PRELOAD=$PWD/../wrapper/tcp_ip_wrapper.so ./multi_client_server 6543
