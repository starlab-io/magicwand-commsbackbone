#!/usr/bin/env bash

PERF_PORT=80
PERF_SERVER="apache"
PERF_NUM_CONNS=12000
PERF_RATE=100
PERF_TIMEOUT=5
PERF_RUN_OPTS="-v"

echo "Starting HTTPERF"
echo "     server: $PERF_SERVER"
echo "       port: $PERF_PORT"
echo "      conns: $PERF_NUM_CONNS"
echo "       rate: $PERF_RATE"
echo "    timeout: $PERF_TIMEOUT"
echo "    options: $PERF_RUN_OPTS"

httperf $PERF_RUN_OPTS --server $PERF_SERVER --port $PERF_PORT --num-conns $PERF_NUM_CONNS --rate $PERF_RATE --timeout $PERF_TIMEOUT > /var/log/httperf/performance.log
parse_httperf.py /var/log/httperf/performance.log /var/log/httperf/performance.csv
gnuplot /usr/local/src/performance_plot.gp