#!/bin/sh

echo ""
echo "Running ld-preloaded tcp/ip app ..."
echo ""

sudo LD_PRELOAD=$PWD/tcp_ip_wrapper.so ./test_client
