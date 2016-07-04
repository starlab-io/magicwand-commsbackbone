#!/usr/bin/env bash

echo "Starting Apache Performance Logging"
expand.sh
`vmstat 2 -t > /var/log/apacheperf/performance.log &` ; httpd-foreground