#!/usr/bin/env bash


echo "Starting Apache Performance Logging"

`vmstat 2 -t > /var/log/apacheperf/performance.log &` ; httpd-foreground
# httpd-foreground