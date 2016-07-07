#!/usr/bin/env bash

echo "Point your browser to http://localhost:8080 for performance data"

pushd display

python -m SimpleHTTPServer 8080

popd
