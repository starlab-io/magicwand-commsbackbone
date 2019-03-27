# Magicwand INS Software Stack Test Plan

***

### Continuous Integration
Use Jenkins to automate regression testing.
* Could Jenkins run test on our dev box, check who the commit came from ?
* Where is the Jenkins server located ? Could we setup a dev system just for CI ?
* Maybe we each one of us has a "testing" pvm on our dev box that jenkins spins up ?
* compile mwcomms, shim and ins code
* load/unload mwcomms driver in PVM
* start/destroy INS VM
* full stack testing (load mwcomms, start INS, start apaches, run ab, shutdown apache/INS
* run functional, stress and perf testing in environment if possible

### Base Integration - Single INS
Basic regression testing on a single INS instance configuration running Apache2 application.

**Configuration -----**
* Single INS instance
* Single PVM
* TCP/IP Shim Library
* Apache2 + WordPress
* Default Apache2 configuration (including mpm_prefork)

**Testing -----**
* Launch Firefox load apache2 web page multiple times (firefox disk/memory caching disabled)
* Run apache benchmark in a loop for one hour (ab -n 500 -c 10 http://xd3-pvm:80/ ; sleep 300)
* Run apache benchmark in a loop for 25 iterations (ab -n 500 -c 10 http://xd3-pvm:80/ ; stop apache2 ; start apach2)
* Start/stop full stack several times (load mwcomms, start INS, start apache2, run ab, stop apache2, unload mwcomms, destroy INS)
* Performance testing (compare non-xd3 stack with xd3 stack using "ab")

**Monitoring -----**
* Firefox web browser display
* Apache Benchmark output
* mwcomms driver messages in /var/log/kern.log
* INS logging
* TCP wrapper logging (/var/log/output/ins_*.log)
* Monitor Apache2 related processes with "htop"

**Verification -----**
* Firefox displays WordPress home page correctly, no hangs, no long pauses while loading
* ApacheBench runs to completion and has zero Failed requests, no hangs or long pauses
* Apache2 shutdowns properly with no long pauses, warnings or errors
* TCP wrapper unloads properly with no hangs, warnings or errors
* The mwcomms driver unloads properly with no hangs, long pauses, warnings or errors
* INS instance is destroyed with no hangs, long pauses, warnings or errors
* No zombie Apache2 related process after shutdown

**Enhancements -----**
* Add testing of multiple INS instances using "frontend" utility (maximum INS instances = ?)

***

### Base Integration - Multiple INS
Basic regression testing on a multiple INS instance configuration running Apache2 application.

**Configuration -----**
* Multiple INS (max instances ?)
* Single PVM
* Apache2 + WordPress
* TCP wrapper library
* Default Apache2 configuration (including mpm_prefork)

**Testing -----**
* Launch Firefox load apache2 web page multiple times (firefox disk/memory caching disabled)
* Run apache benchmark in a loop for one hour (ab -n 500 -c 10 http://xd3-pvm:80/ ; sleep 300)
* Run apache benchmark in a loop for 25 iterations (ab -n 500 -c 10 http://xd3-pvm:80/ ; stop apache2 ; start apach2)
* Start/stop full stack several times (load mwcomms, start INS, start apache2, run ab, stop apache2, unload mwcomms, destroy INS)
* Performance testing (compare non-xd3 stack with xd3 stack using "ab") ?

**Monitoring -----**
* Firefox web browser display
* Apache Benchmark output
* mwcomms driver messages in /var/log/kern.log
* INS logging
* TCP wrapper logging (/var/log/output/ins_*.log)
* Monitor Apache2 related processes with "htop"
* How do we monitor the number of INS instances running ?

**Verification  -----**
* Firefox displays WordPress home page correctly, no hangs, no long pauses while loading
* ApacheBench runs to completion and has zero Failed requests, no hangs or long pauses
* Apache2 shutdowns properly with no long pauses, warnings or errors
* TCP wrapper unloads properly with no hangs, warnings or errors
* mwcomms driver unloads properly with no hangs, long pauses, warnings or errors
* All INS instances are destroyed with no hangs, long pauses, warnings or errors
* No zombie Apache2 related process after shutdown

**Enhancements -----**
* Add testing of multiple INS instances using "frontend" utility (maximum INS instances = ?)

***

### Basic Performance Regression Testing
Basic performance regression test comparing Apache2 performance with and without INS stack.

**Configuration -----**
*

**Testing -----**
*

**Monitoring -----**
*

**Verification  -----**
*

**Enhancements -----**
*

***

### Test Section Template
Test section template description.

**Configuration -----**
*

**Testing -----**
*

**Monitoring -----**
*

**Verification  -----**
*

**Enhancements -----**
*

***
