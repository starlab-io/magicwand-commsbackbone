
Summary of Work
===============

During the project, Star Lab researchers successfully implemented and tested the full application agnostic isolated network stack (INS) with multiple applications. This includes using the NetFlow API to monitor application network traffic and running multiple front end unikernels to handle large request load.

Specific accomplishments include:

1.  Application agnostic shared library interface to the isolated network stack tested with Apache2 and NGINX web servers;

2. Loaded MwComms linux kernel driver in a Xen VM and communicated with multiple front end Xen VMs running fully tunable Unikernels.

3. Using the Xen high speed memory channel for transferring data between the protected VM kernel driver and each INS instance.

4. Monitored protected application network traffic using the MwComms driver NetFlow interface. Successfully sent request/response  messages over NetFlow interface in preparation for handling mitigation commands.

5. Run multiple unikernel front end Xen VMs to handle network socket requests. Each unikernel is separately tunable by MwComms using the NetFlow interface.

6. Implemented a front end load balancer used to spin up new INS instances that dynamically handle network traffic flowing to the protected application. Tested with 23 INS instances supporting 10,350 simultaneous connections.

7. Designed and implemented protocol for communicating socket call parameters and return values between Xen paravirtualized guests over high speed Xen shared memory ring buffer.

8. Created a custom virtual file system to reduce ring buffer congestion and improve performance by eliminating the need for poll() function calls to send a request over the ring buffer.

9. Modified Rumprun unikernel to allow over 4000 connections to one INS instance.

10. Limited INS to roughly 10ms per transaction overhead in data throughput tests.
