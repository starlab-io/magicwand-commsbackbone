
Summary of Work
===============

The Star Lab team has successfully implemented and tested the full application agnostic isolated network stack (INS) with multiple applications. This includes using the NetFlow API to monitor application network traffic and running multiplefront end UniKernel VMs to handle large request load.

This includes the following accomplishments:

1.  Application agnostic shared library interface to the isolated network stacktested with Apache2 and NGINX web servers.

2. Loaded MwComms linux kernel driver in a Xen VM and communicated with multiplefront end Xen VMs running fully tunable Unikernels.

3. Using the Xen high speed memory channel for transferring data between the protected VM kernel driver and each INS instance.

4. Monitored protected application network traffic using the MwComms driver NetFlowinterface. Successfully sent request/response  messages over NetFlow interface in preparation for handling mitigation commands.

5. Run multiple UniKernel front end Xen VMs to handle network socket requests. EachUniKernel is separately tunable by MwComms using the NetFlow interface.

6. Implemented a front end load balancer used to spin up new INS instances thatdynamically handle network traffic flowing to the protected application. Testedwith 23 INS instances supporting 10,350 simultaneous connections.

7. Designed and implemented protocol for communicating socket call parameters and returnvalues between Xen paravirtualized guests over high speed Xen shared memory ring buffer.

8. Created a custom virtual filesystem to reduce ring buffer congestion and improve performance.
Previously a message had to traverse the ring buffer, wait through a poll call andreturn again. With the custom VFS, polling is done on custom file objects that get updatedbehind the scenes.

9. Modified Rump UniKernel maximum allowable open files value which originally limited thesystem to have around 200 open sockets to allow over 4000 connections to one INS instance.

10. Limited INS to roughly 30ms per transaction overhead in data throughput tests.

