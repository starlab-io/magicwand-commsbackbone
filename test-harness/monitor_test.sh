#!/usr/bin/env bash

# make sure log directories exist
mkdir -p ./log/httperf/
mkdir -p ./log/apacheperf/
mkdir -p ./log/apachepin/

# clean out any historic PIN files
rm ./log/apachepin/trace.*

if [ -z "$TEST_DURATION" ]; then
    LORIS_TEST_DURATION=120
else
    LORIS_TEST_DURATION=$TEST_DURATION
fi

export LORIS_TEST_DURATION

SYNC_INTERVAL=$(expr $LORIS_TEST_DURATION / 10)

echo "Preparing..."
echo "  ! running performance collection for $LORIS_TEST_DURATION seconds"

rm ./log/httperf/done

echo "Spinning up test instances"
docker-compose up -d

echo "Monitoring test (sync interval $SYNC_INTERVAL seconds)..."

while [ ! -f ./log/httperf/done ] ; do
    sleep $SYNC_INTERVAL
    echo "  > running"
done

echo "  + collating HTTPerf results"

echo "  ! waiting for PIN & vmstat to complete tracing (this could take awhile)"

while [ ! -f ./log/apacheperf/done ] ; do
    sleep $SYNC_INTERVAL
    echo "  > running"
done

echo "  + collating ApachePerf results"
./apacheperf/parse_apacheperf.py ./log/apacheperf/performance.log ./log/apacheperf/performance.csv ./log/apacheperf/performance.json

echo "  + collating GoLoris results"
./goloris/parse_goloris.py ./log/goloris/performance.log ./log/goloris/performance.csv ./log/goloris/performance.json

echo "  ! Tearing down test harness containers"

docker-compose down

echo "  . done"

echo "!! If you want to save these test results don't forget to run ./archive_test"

echo "!! Archived test results can be viewed using ./view_tests.sh"