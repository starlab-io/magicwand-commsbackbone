MAGICWAND Test Harness
=======

# Quickstart

```sh
cd test-harness
docker-compose up -d
./stats.sh
```

# Full Docker Instructions

The Apache and GoLoris components each have a Dockerfile, and can be run 
individually or combined via _docker-compose_. These instructions assume
you have _Docker_ installed.

> If you're running Docker on a Mac, make sure you're in a Docker
> Quickstart Terminal or similar environment, otherwise you won't be
> able to reach the Docker _daemon_.

## Getting the Docker Images

## from Docker Hub

Most of the bash examples below will automatically download the Docker
images you need (or use existing ones from your local cache). You can always
manually download them with:

```sh
docker pull patricknevindwyer/apache-harness:latest
docker pull patricknevindwyer/goloris-harness:latest
```

## by building them

See the sections below on Apache and GoLoris for build instructions.

## GETTING OUT OF TROUBLE

It's possible to get somewhat stuck in Docker, where you want to build/run/stop
an image, but Docker refuses (for various good and bad reasons). As a fall
back, you can always do the following to get to a fairly clean state. Given a troublesome
running image named **apache-harness**:

```sh
docker stop apache-harness
docker rm apache-harness
```


## Running the Tests

Running the test harness is fairly simple - from the *test-harness* directory:

```sh
docker-compose up -d
```

starts the tests. You can then check on Apache with the *stats.sh* script:

```sh
./stats.sh
    132 streams active in Apache
```

If you want to more deeply inspect the Apache environment, you can log
into the running Apache instance with:

```sh
docker-compose exec apache bash
```

You can also log into GoLoris:

```sh
docker-compose exec goloris bash
```

When you're done running the tests you can bring the harness down with:

```sh
docker-compose down
```

> If you don't explicity bring the composed services down, they will
> continue to run, which can cause odd problems if you're building or
> modifying the images for those services.

## Running GoLoris in Stand Alone mode

For various reasons it can be useful to directly work with the GoLoris
image. The following instructions assume you are in the _test-harness/goloris_
directory.

### Manually Running GoLoris

The GoLoris Docker image doesn't have a default command to run, so you 
need to specify one when running manually. You can run GoLoris from
the _goloris_ directory with:

```sh
docker run -d --name goloris patricknevindwyer/goloris-harness:latest /goloris -victimUrl="http://{IP ADDRESS}:9000"
```

You can get the target IP address from _Docker Machine_:

```sh
docker-machine ip
```

### Logging into GoLoris environment

You can log into the GoLoris instance using the _docker exec_ command:

```sh
docker exec -it goloris bash
```

> Note that this command line has different parameters than the docker-compose
> command that logs into an active image. The _-it_ flags tell docker
> to use an interactive TTY device, which is used by default in
> docker-compose.

### Building the GoLoris Docker Image

You can manually build the GoLoris Docker image instead of downloading it
from Docker Hub. From the _test-harness/goloris_ directory:

```sh
docker build -t patricknevindwyer/goloris-harness:latest .
```

You now have a built Docker image, which can be referenced by name (as is
used in the other bash examples throughout the documentation).

## Running Apache in Stand Alone mode

For various reasons it can be useful to directly work with the GoLoris
image. The following instructions assume you are in the _test-harness/apache_
directory.

### Manually Running Apache

The Apache image _has_ a default command to spin up the web server, so
you can start the Apache image with:

```sh
docer run -d --name apache  -p 9000:80 -t patricknevindwyer/apache-harness:latest
```

### Logging into Apache environment

You can access the running Apache instance using the _docker exec_ command:

```sh
docker exec -it apache bash
```

> Note that this command line has different parameters than the docker-compose
> command that logs into an active image. The _-it_ flags tell docker
> to use an interactive TTY device, which is used by default in
> docker-compose.

### Building the Apache Docker Image

You can manually build the Apache Docker image instead of downloading it
from Docker Hub. From the _test-harness/apache_ directory:

```sh
docker build -t patricknevindwyer/apache-harness:latest .
```


## Useful Docker commands

See the state of running images:

```sh
> docker ps
CONTAINER ID        IMAGE                                      COMMAND                  CREATED             STATUS              PORTS                   NAMES
12a36f254b0d        patricknevindwyer/goloris-harness:latest   "/goloris -victimUrl="   8 minutes ago       Up 8 minutes                                goloris-test
a31b6586ed5d        patricknevindwyer/apache-harness:latest    "httpd-foreground"       10 minutes ago      Up 10 minutes       0.0.0.0:10000->80/tcp   apache-harness-test
```

Pull STDOUT/STDERR logs from running image:

```sh
> docker logs goloris-test
```

Start a _bash_ session on a running image:

```sh
> docker exec -it goloris-test bash
root@12a36f254b0d:/#  
```


Get a **top** like status view of running images:

```sh
> docker stats
```

# Vagrant Instructions


## Start target and attacker VMs

```
# Start Apache 2.2.11 server to be attacked (192.168.33.10)
pushd test-harness/apache
vagrant up
popd

# Start the attacker box (192.168.33.11)
pushd test-harness/goloris
vagrant up
popd
```

If the `vagrant up` fails, you may need to download the base box. Run `vagrant box add ubuntu/trusty32`.

### Start Apache 2.2.11 (vagrant provisioning should do this already)

```
sudo /usr/local/apache2/bin/apachectl start
```

### Attack Apache with goloris

```
cd test-harness/goloris
vagrant ssh
cd gocode/bin
./goloris -victimUrl="http://192.168.33.10:80"
```

### See the impact

On the machine:

```
cd test-harness/apache
vagrant ssh

# Maxes out around 256 processes
ps aux | grep httpd | wc -l
```

Or by fetching the web page from your host machine

http://192.168.33.10

### Installing an old version of nginx (not currently used)

```
http://hg.nginx.org/nginx/archive/release-1.5.8.tar.gz
./auto/configure --without-http_rewrite_module --without-http_gzip_module
```
