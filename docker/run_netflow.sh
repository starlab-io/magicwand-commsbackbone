#!/bin/bash

echo "Starting netflow collection"


#start ceph sync
./upload.sh &
tail -f /code/archive/netflow.json &
python mw_netflow.py > /code/archive/netflow.json
