#!/usr/bin/env bash

export LORIS_TEST_DURATION=120
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

while [ ! -f ./log/apacheperf/done ] ; do
    sleep $SYNC_INTERVAL
    echo "  > running"
done

echo "  + collating ApachePerf results"
./apacheperf/parse_apacheperf.py ./log/apacheperf/performance.log ./log/apacheperf/performance.csv ./log/apacheperf/performance.json

echo "  ! Tearing down test harness containers"

docker-compose down

echo "  > moving performance data for display"

cp ./log/httperf/performance.csv ./display/data/httperf/
cp ./log/apacheperf/performance.csv ./display/data/apacheperf/


echo "Point your browser to http://localhost:8080 for performance data"

pushd display

python -M SimpleHTTPServer 8080

popd

echo "  . done"
