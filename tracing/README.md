MAGICWAND Tracing 
=======

## Start the Apache 2.2.11 server VM and log in

```
cd tracing
vagrant up
vagrant ssh
```
### Start Apache 2.2.11 with pin (vagrant provisioning should do this already)

```
sudo apachectl pin
```

### See what's running
```
ps -ef | grep httpd
```

### Stop Apache 2.2.11
```
sudo apachectl stop
```

### Check out output files
```
ls ~/output
### Attack Apache with goloris

```
Files should be labeled "trace.<pid>"
