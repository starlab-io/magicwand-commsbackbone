#!/bin/bash

IP=10.30.30.134


rumprun -S xen -di -M 256 -N test-rump\
	-I xen0,xenif \
	-W xen0,inet,static,$IP/24 \
	./accf_data_test.run "80"


#sudo /home/alex/rump/rumprun/rumprun/bin/rumprun xen -di \
#	-I xen0,xenif \
#	-W xen0,inet,dhcp \
#	./test.run
