#!/bin/sh

echo ""
echo "Running ld-preloaded tcp/ip app ..."
echo ""

sudo LD_PRELOAD=$PWD/wrap_test_server.so ./test_server
