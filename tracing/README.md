MAGICWAND Tracing 
=======

## Relevant Directories

The callgrind directory is there only for historical reasons. I may actually remove it soon. The real tracing tool is called "callgraph" (which is actually a horrible name, because it generates a trace, not a graph), and is found in the pin directory. The datagen directory contains toy executable code and scripts to generate trace data from them. 

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

### If you check the output directory, it should look something like this:
```
vagrant@vagrant-ubuntu-trusty-32:~$ ls -l output/
total 7528
-rw-r--r-- 1 root root 7706634 May 19 22:36 trace.21489
-rw-r--r-- 1 root root       0 May 19 22:36 trace.21492
-rw-r--r-- 1 root root       0 May 19 22:36 trace.21493
-rw-r--r-- 1 root root       0 May 19 22:36 trace.21494
-rw-r--r-- 1 root root       0 May 19 22:36 trace.21495
-rw-r--r-- 1 root root       0 May 19 22:36 trace.21496
-rw-r--r-- 1 root root       0 May 19 22:36 trace.21497
```

### The files are empty because they currently on write on process termination. So, Stop Apache 2.2.11
```
sudo apachectl stop
```

### Check out output files again (they should be populated now)
```
vagrant@vagrant-ubuntu-trusty-32:~$ ls -l output/
total 13884
-rw-r--r-- 1 root root 7706634 May 19 22:36 trace.21489
-rw-r--r-- 1 root root 4254464 May 19 22:37 trace.21492
-rw-r--r-- 1 root root  448098 May 19 22:37 trace.21493
-rw-r--r-- 1 root root  448098 May 19 22:37 trace.21494
-rw-r--r-- 1 root root  448098 May 19 22:37 trace.21495
-rw-r--r-- 1 root root  448828 May 19 22:37 trace.21496
-rw-r--r-- 1 root root  448098 May 19 22:37 trace.21497
```
Files should be labeled "trace.\<pid\>"
