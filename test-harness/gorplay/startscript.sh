#!/usr/bin/env bash

echo "gor go!"

cat log/gorcatch.gor
# ./gor --input-raw :80 --output-http="http://172.17.0.2:80" &
./gor --input-file log/recorded_0.gor --output-http="http://172.17.0.2:80" &
httpd-foreground