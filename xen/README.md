
# Open Questions

* Apache in a Unikernel?
* How does Xen vchan fit in?
* Can you replace drivers/memory buses between docker containers?
* Rumpkernel seems to play well with Docker - can we somehow leverage that to do our integration testing on Xen?

# Labs Hardware

Two integration hosts:

> host: xd3-r805-1
> data: 172.20.11.205
> ilo: 172.20.12.162


> host: xd3-r805-2
> data: 172.20.10.161
> ilo: 172.20.12.156

The second host (_-2_) is currently prepped for running a unikernel.

# Prepping an Ubuntu Dom0

Assuming Xen and an Ubuntu barebones Dom0 are already running. 

Basic prep:

```sh
sudo apt-get install git
sudo apt-get update && sudo apt-get upgrade
sudo apt-get install -y gcc make libxen-dev genisoimage g++ build-essential libtool libpcre3-dev autoconf libncurses5-dev openssl libssl-dev fop xsltproc unixodbc-dev

mkdir unikernels
cd unikernels
```

Rump install
```sh
git clone http://repo.rumpkernel.org/rumprun
cd rumprun
git submodule update init
CC=gcc ./build-rr.sh xen
```

Output of Rump install should look like:

```sh
>>
>> Finished ./build-rr.sh for xen
>>
>> For Rumprun developers (if you're not sure, you don't need it):
. "/home/invincea/unikernels/rumprun/./obj-amd64-xen/config"
>>
>> toolchain tuple: x86_64-rumprun-netbsd
>> cc wrapper: x86_64-rumprun-netbsd-gcc
>> installed to "/home/invincea/unikernels/rumprun/./rumprun"
>>
>> Set tooldir to front of $PATH (bourne-style shells)
. "/home/invincea/unikernels/rumprun/./obj-amd64-xen/config-PATH.sh"
>>
>> ./build-rr.sh ran successfully
```

Modify the user `~/.profile` to include:

```sh
PATH="$HOME/unikernels/rumprun/rumprun/bin:$PATH"
```

and make sure to source the path changes:

```sh
source ~/.profile
```

# Installing a Redis unikernel

For testing purposes, you can build and install a Redis unikernel:

```sh
cd ~/unikernels
git clone https://github.com/rumpkernel/rumprun-packages.git
cd rumprun-packages
```

Edit the config.mk.dist file and set the _RUMPRUN_TOOLCHAIN_TUPLE_ to *x86_64-rumprun-netbsd*

And then build Redis:

```sh
mv config.mk.dist config.mk
cd redis
make
```

Now we can bake the Redis binary into a unikernel:

```sh
rumprun-bake xen_pv bin/redis-server.bin bin/redis-server
```

# Running the kernel

From the `~/unikernels/rumprun-pacakges/redis` directory load the Redis backed into Xen

```sh
 sudo -E /home/invincea/unikernels/rumprun/rumprun/bin/rumprun xen -N redis -I xen0,xenif,mac=00:11:22:33:44:55,bridge=xenbr0 -W xen0,inet,dhcp -d bin/redis-server.bin
```

You can check the kernel is running with:

```sh
> sudo xl list
Name                                        ID   Mem VCPUs	State	Time(s)
Domain-0                                     0  4096    12     r-----   16052.1
redis                                       20    64     1     -b----       0.1
```

Of special note - the Redis instance is running with *64mb* of RAM and a single VCPU. The actual binary size of this kernel is ~23mb.



Find the IP address (the Redis kernel picked up an IPv4 address via DHCP) of the kernel. From the Dom0 host:

```sh
> arp -a
? (172.20.11.216) at 00:11:22:33:44:55 [ether] on eth0
Patricks-MBP.labs (172.20.11.193) at 38:c9:86:2e:fb:75 [ether] on eth0
? (172.20.0.90) at 0c:c4:7a:a9:5b:8d [ether] on eth0
ns.labs (172.20.0.11) at 00:16:3e:40:04:ce [ether] on eth0
android-51f498731f3b0a83.labs (172.20.10.40) at 90:b6:86:4f:83:27 [ether] on eth0
? (172.20.12.23) at f4:5c:89:c5:ed:21 [ether] on eth0
? (172.20.12.68) at 68:5b:35:ab:b1:2b [ether] on eth0
```

When we launched the Redis unikernel, we assigned a MAC address of `00:11:22:33:44:55`, which we can see mapped to `172.20.11.216`. We can verify that
Redis is running by launching a Redis _cli_ from a local laptop or other machine:

```sh
🚀  ~/bin/redis-cli -h 172.20.11.216
172.20.11.216:6379> ping
PONG
172.20.11.216:6379> set foo bar
OK
172.20.11.216:6379> get foo
"bar"
172.20.11.216:6379> 
```

When we're finished with the Redis instance, we can destroy it with:

```sh
> sudo xl destroy redis
```
