#!/bin/bash

INDEXDIR=/tmp
FILEDIR=$PWD/files
RESULTSDIR=$PWD/results
TYPE=
ATTEMPTS=1

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
Nfi

ABS_PATH=$RESULTSDIR/$TYPE.dat
mkdir -p $RESULTSDIR

echo "#Size(bytes)      Average Response Time ( $ATTEMPTS reqests )" > $ABS_PATH

for i in $( ls $FILEDIR | sort -n ); do
    
    scp $FILEDIR/$i $USER@$PVM_IP:$INDEXDIR/index.html >> /dev/null
    if [ $? -eq 0 ]; then
        echo "Command failed"
        break
    fi

    AVG_RESPONSE=`ab -n $ATTEMPTS "http://$ADDR/" | grep "\[ms\] (mean)" | awk '{ print $4 }'`
    if [ $? -eq 0 ]; then
        echo "Command failed"
        break
    fi

    SIZE=$i
    echo "$SIZE               $AVG_RESPONSE" >> $ABS_PATH

done
