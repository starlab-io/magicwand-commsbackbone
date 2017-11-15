#!/bin/bash

INDEXDIR=/tmp
FILEDIR=$PWD/files
RESULTSDIR=$PWD/results
ATTEMPTS=10

if [ ! -d "$FILEDIR" ]; then
    echo ""
    echo "Please run the make_files.sh script first"
    echo ""
    exit 0
fi

if [ -z $PVM_IP ]; then
   echo "Failure: PVM_IP must be defined in env"
   exit 1
fi

if [ -z $RUMP_IP ]; then
   echo "Failure: RUMP_IP must be defined in env"
   exit 1
fi

if [ -z $1 ]; then
    echo
    echo "Do you want to test the raw server times or the rump server times?"
    echo "usage: run_benchmark.sh <rump|raw>"
    echo
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

echo "#Size(bytes)      Average Response Time ( $ATTEMPTS reqests )" > $ABS_PATH

for i in $( ls $FILEDIR | sort -n ); do
    
    scp $FILEDIR/$i $PVM_USER@$PVM_IP:$INDEXDIR/index.html >> /dev/null
    RC=$?
    if [ $RC -ne 0 ]; then
        echo "Command failed"
        exit $RC
    fi

    RAW_INPUT=$(ab -n $ATTEMPTS "http://$ADDR/")
    RC=$?
    if [ $RC -ne 0 ]; then
        echo "Command failed"
        exit $RC
    fi

    AVG_RESPONSE=`echo "$RAW_INPUT" | grep "\[ms\] (mean)" | awk '{ print $4 }'`
    RC=$?
    if [ $RC -ne 0 ]; then
        echo "Command failed"
        exit $RC
    fi

    SIZE=$i
    echo "$SIZE               $AVG_RESPONSE" >> $ABS_PATH

done
