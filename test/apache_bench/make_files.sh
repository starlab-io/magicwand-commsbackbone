#!/bin/bash

FILEDIR=./files
ONE_HUNDRED_BYTES="0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"

mkdir -p ./files

# One file: 1 byte
#echo -n "1" > $FILEDIR/1

# One file: 5000 bytes
for i in `seq 5000 5000 5000`;
do
    echo -n "" > $FILEDIR/$i

    for j in `seq 0 $(( ( i - 1 ) / 100 ))`;
    do
        echo -n "$ONE_HUNDRED_BYTES" >> $FILEDIR/$i
    done
done


FIVE_THOUSAND_BYTES=`cat $FILEDIR/5000`

# Several files: 250,000 - 5,000,000 in 250,000 increments
for i in `seq 250000 250000 5000000`;
do
    echo -n "" > $FILEDIR/$i

    for j in `seq 0 $(( ( i - 1 ) / 5000 ))`;
    do
        echo -n "$FIVE_THOUSAND_BYTES" >> $FILEDIR/$i
    done
done

