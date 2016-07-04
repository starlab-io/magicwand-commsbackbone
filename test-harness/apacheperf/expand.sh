#!/bin/bash
mkdir /usr/local/apache2/htdocs/static/
for i in `seq 1 10`;
do
	dd if=/dev/zero of=/usr/local/apache2/htdocs/static/$((2**${i}))k.dat bs=1024 count=$((2**${i}))
done  