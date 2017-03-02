#!/bin/bash

INDEX=0
FILEDIR=./files
ONE_HUNDRED_BYTES="0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"

mkdir -p ./files

for i in `seq 0 100 5000`;
do
    echo -n "" > $FILEDIR/$i

    for j in `seq 0 $(( ( i - 1 ) / 100 ))`;
    do
        echo -n "$ONE_HUNDRED_BYTES" >> $FILEDIR/$i
    done
done



ONE_THOUSAND_BYTES=`cat $FILEDIR/1000`

for i in `seq 6000 1000 50000`;
do
    echo -n "" > $FILEDIR/$i

    for j in `seq 0 $(( ( i - 1 ) / 1000 ))`;
    do
        echo -n "$ONE_THOUSAND_BYTES" >> $FILEDIR/$i
    done
done


FIFTY_THOUSAND_BYTES=`cat $FILEDIR/50000`

for i in `seq 100000 50000 1000000`;
do
    echo -n "" > $FILEDIR/$i

    for j in `seq 0 $(( ( i - 1 ) / 50000 ))`;
    do
        echo -n "$FIFTY_THOUSAND_BYTES" >> $FILEDIR/$i
    done
done

echo -n "1" > $FILEDIR/1
rm -f $FILEDIR/0
