#!/usr/bin/env bash


echo "Starting Apache Performance Logging"

`vmstat 2 > /var/log/apacheperf/performance.log &` ; httpd-foreground
# httpd-foreground