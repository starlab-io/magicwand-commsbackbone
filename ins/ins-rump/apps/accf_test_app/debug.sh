#!/bin/bash

IP=10.30.30.134
TARGET=accf_data_test.run

echo "gdb -tui -ex 'target remote:1234' $TARGET"

rumprun -S xen -dip -D 1234 -M 256 -N test-rump \
	-I xen0,xenif \
	-W xen0,inet,static,$IP/24 \
	./$TARGET "80"


#sudo /home/alex/rump/rumprun/rumprun/bin/rumprun xen -di \
#	-I xen0,xenif \
#	-W xen0,inet,dhcp \
#	./test.run
