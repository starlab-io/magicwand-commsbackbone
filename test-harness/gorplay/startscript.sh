#!/usr/bin/env bash

echo "gor gogo!"

# cat log/gorcatch.gor
# ./gor --input-raw :80 --output-http="http://172.17.0.2:80" &
./gor --input-file log/${1} --output-http="${2}" &
sleep 3600