#!/usr/bin/env python

import subprocess
import os
import time
import sys


pvm_ip = os.environ['PVM_IP'];
pvm_user = os.environ['PVM_USER'];
hostname = os.environ['WP_HOSTNAME']

start_docker_command = "docker run --name llvm_xray_apache_wp_debug --net=host --device=/dev/mwcomms -v $PWD/mw-config:/var/mw-config/ -v $PWD/xtube:/code/xtube-live -d wbradmoore/llvm-xray-apache-wp:latest"

stop_docker_command = "docker stop llvm_xray_apache_wp_debug; docker rm llvm_xray_apache_wp_debug"

sudo_pw = raw_input( "Please enter password for user: " + pvm_user + " on remote: " + pvm_ip + "\n");

sudo_prefix = "echo '" + sudo_pw + "' | " + "sudo -S "

for i in range( 0, 25 ) :

    ret = subprocess.call( ["ssh", pvm_user + "@" + pvm_ip, sudo_prefix + start_docker_command ] );


    ret = subprocess.call( ["ab", "-n 1000", "-c 100", "http://" + hostname + "/" ] );


    ret = subprocess.call( ["ssh","-p", sudo_pw, pvm_user + "@" + pvm_ip, sudo_prefix + stop_docker_command] );

    time.sleep(30);
