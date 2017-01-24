#!/bin/sh

echo ""
echo "Running ld-preloaded tcp/ip app ..."
echo ""

sudo LD_PRELOAD=/home/alex/magicwand-commsbackbone/protvm/user/wrapper/tcp_ip_wrapper.so ./test_client
