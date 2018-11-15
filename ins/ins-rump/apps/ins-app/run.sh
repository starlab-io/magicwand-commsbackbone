#!/bin/bash

##
## STAR LAB PROPRIETARY & CONFIDENTIAL
## Copyright (C) 2016, Star Lab — All Rights Reserved
## Unauthorized copying of this file, via any medium is strictly prohibited.
##

netmask_to_cidr () {
  CIDR=0
  x=0$( printf '%o' ${1//./ } )
  while [ $x -gt 0 ]; do
    let CIDR+=$((x%2)) 'x>>=1'
  done
  echo "$CIDR"
}

TARGET="ins-rump.run"
NAME="mw-ins-rump"
MEMORY="512"
NETWORK="xen0,xenif"
PLATFORM="xen"
PORT="2221"
NM=""
IP=""

USAGE="Usage: `basename $0`
  -d : Use DHCP for INS networking
  -s : Use \$RUMP_IP and \$_GW for INS networking (default)
  -S : Use \$RUMP_IP2 and \$_GW for INS networking
  -g : Enable INS debugging with GDB"

DHCP=0
STATIC=0
GDB=""

while getopts ":dsSg" opt; do
  case $opt in
    d)
      if [ $STATIC -gt 0 ] ; then
        echo "Options -d and -s / -S are mutually exclusive !!!"
        exit 1
      elif [ $DHCP -eq 1 ] ; then
        echo "Only specify -d option once !!!"
        exit 1
      fi
      DHCP=1
      ;;
    s)
      if [ $DHCP -eq 1 ] ; then
        echo "Options -d and -s / -S are mutually exclusive !!!"
        exit 1
      elif [ $STATIC -gt 0 ] ; then
        echo "Only specify -s / -S option once !!!"
        exit 1
      fi
      STATIC=1
      IP="${RUMP_IP:foobar}"
      ;;
    S)
      if [ $DHCP -eq 1 ] ; then
        echo "Options -d and -s / -S are mutually exclusive !!!"
        exit 1
      elif [ $STATIC -gt 0 ] ; then
        echo "Only specify -s / -S option once !!!"
        exit 1
      fi
      STATIC=2
      IP="${RUMP_IP2:foobar}"
      NAME="${NAME}-2"
      ;;
    g)
      GDB="-p -D $PORT"
      ;;
    ?|h)
      echo "$USAGE"
      exit 1
      ;;
    \?)
      echo "Invalid option: -$OPTARG"
      exit 1
      ;;
    :)
      echo "Option -$OPTARG requires an argument."
      exit 1
      ;;
  esac
done

if [ $STATIC -eq 1 -a -z "$RUMP_IP" ]; then
    echo "The -s/-S options require RUMP_IP environment variable be set !!!"
    exit 1
fi

if [ $STATIC -eq 2 -a -z "$RUMP_IP2" ]; then
    echo "The -s/-S options require RUMP_IP2 environment variable be set !!!"
    exit 1
fi

if [ $STATIC -gt 0 -a -z "$_GW" ]; then
    echo "The -s/-S options require _GW environment variable be set !!!"
    exit 1
fi

if [ $# -eq 0 ] ; then
  STATIC=1
  IP="${RUMP_IP:foobar}"
fi

if [ $STATIC -gt 0 ]; then
  IF=$(ip route | grep default | awk '{print $5}')
  NM=$(ifconfig $IF | grep -e 'Mask:' | awk '{print $4}' | cut -d":" -f2)
  if [[ ! "$NM" =~ ^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}$ ]] ; then
    echo "The NETMASK '$NM' is malformed, cannot convert to CIDR !!!"
    exit 1
  fi
  CIDR=$(netmask_to_cidr $NM)
  INTERFACE="xen0,inet,static,$IP/$CIDR,$_GW"
else
  INTERFACE="xen0,inet,dhcp"
fi

echo "Starting INS instance -----"
echo "Name   : $NAME"
echo "Network: $INTERFACE"
echo "MEMORY : $MEMORY"

if [ -z "$GDB" ] ; then
  echo "DEBUG  : disabled"
else
  echo "DEBUG  : enabled"
  echo "GDB CMD: gdb -tui -ex 'target remote localhost:$PORT' $NAME"
fi

# rumprun script arguments:
# -S execute sudo where potentially necessary
#
# rumprun guest arguments:
# -p creates the guest but leaves it paused
# -D attaches a gdb server on port to the guest
# -d destroys the guest on poweroff
# -i attaches to guest console on startup
# -M set the guest's memory to mem megabytes, default is 64
# -N set the guest's name to name, default is rumprun-APP
# -I create guest network interface and attach the user-specified
# -W configure network interface new style
#    inet,dhcp - IPv4 with DHCP
#    inet,static,addr/mask[,gateway] - IPv4 with static IP

rumprun -S $PLATFORM -d -i $GDB -M $MEMORY -N $NAME -I $NETWORK -W $INTERFACE $TARGET

