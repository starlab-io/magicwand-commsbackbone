#!/bin/bash

INDEXDIR=$MWROOT/protvm/user/http_server/
FILEDIR=$PWD/files
RESULTSDIR=$PWD/results
TYPE=

if [ ! -d "$FILEDIR" ]; then
    
    echo ""
    echo "Please run the make_files.sh script first"
    echo ""

    exit 0
fi


if [ "$1" = "rump" ]; then

    ADDR=$RUMP_IP
    TYPE="rump"
else
    ADDR=$PVM_IP
    TYPE="raw"
fi

ABS_PATH=$RESULTSDIR/$TYPE.dat
mkdir -p $RESULTSDIR

echo "#Size(bytes)      Average Response Time ( 10 reqests )" > $ABS_PATH

for i in $( ls $FILEDIR | sort -n ); do
    
    scp $FILEDIR/$i $USER@$PVM_IP:$INDEXDIR/index.html >> /dev/null

    AVG_RESPONSE=`ab -n 10 "http://$ADDR/" | grep "\[ms\] (mean)" | awk '{ print $4 }'`
    SIZE=$i

    echo "$SIZE               $AVG_RESPONSE" >> $ABS_PATH

done
