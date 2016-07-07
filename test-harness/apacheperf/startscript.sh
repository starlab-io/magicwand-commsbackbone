#!/usr/bin/env bash

rm /var/log/apacheperf/done

if [ -z "$LORIS_TEST_DURATION" ]; then
    TEST_DURATION=120
else
    TEST_DURATION=$LORIS_TEST_DURATION
fi

echo "Starting Apache Performance Logging (logging for $TEST_DURATION seconds)"
expand.sh
echo "test-duration $TEST_DURATION" > /var/log/apacheperf/performance.log
`vmstat 1 $TEST_DURATION -t >> /var/log/apacheperf/performance.log &` ; httpd-background

echo "Waiting $TEST_DURATION seconds for log generation"

sleep $TEST_DURATION

# sleep a bit extra to get the wind down from Apache performance
echo "Performance Logging Complete"

touch /var/log/apacheperf/done