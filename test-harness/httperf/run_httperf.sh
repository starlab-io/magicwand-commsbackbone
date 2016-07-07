#!/usr/bin/env bash

rm /var/log/httperf/done

if [ -z "$LORIS_TEST_DURATION" ]; then
    TEST_DURACTION=120
else
    TEST_DURATION=$LORIS_TEST_DURATION
fi

PERF_PORT=80
PERF_SERVER="apache"
PERF_RATE=100
PERF_NUM_CONNS=$(expr $PERF_RATE \* $TEST_DURATION)
PERF_TIMEOUT=5
PERF_RUN_OPTS="-v"

echo "Starting HTTPERF"
echo "   duration: $TEST_DURATION"
echo "     server: $PERF_SERVER"
echo "       port: $PERF_PORT"
echo "      conns: $PERF_NUM_CONNS"
echo "       rate: $PERF_RATE"
echo "    timeout: $PERF_TIMEOUT"
echo "    options: $PERF_RUN_OPTS"

# We don't get useful timestamps from httperf, so we need to estimate with a bounding start time
echo start-time `date -u +%Y-%m-%dT%H:%M:%S` > /var/log/httperf/performance.log
httperf $PERF_RUN_OPTS --server $PERF_SERVER --port $PERF_PORT --num-conns $PERF_NUM_CONNS --rate $PERF_RATE --timeout $PERF_TIMEOUT >> /var/log/httperf/performance.log
parse_httperf.py /var/log/httperf/performance.log /var/log/httperf/performance.csv

echo "DONE"

touch /var/log/httperf/done