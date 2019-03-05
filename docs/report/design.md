
Design
======

## Shim

The shim works by intercepting all socket related syscalls and forwarding the calls pertaining to IPv4 to the mwcomms driver by converting them to the mwcomms message format defined in message_types.h or by calling the appropriate ioctls on the mwcomms device.


## Mwcomms driver

Location:

```
/protvm/kernel/mwcomms
```

### Introduction

The MagicWand driver (Linux kernel module, or LKM) for the protected virtual machine (PVM) facilitates the passing of requests from a protected application to one or more Isolated Network Stacks (INSs, currently backed by Rump unikernels on the same hardware). It also facilitates the delivery of a response, produced by an INS, to the protected application that expects it. This LKM is intended to be exercised by a custom shared object ("shim") that the protected application loads via LD_PRELOAD. The shim is available in this repo.

The LKM supports multithreading / multiprocessing - e.g a multi-threaded application above it can read/write to its device, and each request (write) can expect to receive the corresponding response (via read). It is designed to be fast, with expected slowdowns in the 10s of milliseconds relative to native speeds.

The LKM supports a handshake with another Xen virtual machine, the INS unikernel agent (implemented on the Rump unikernel). The handshake involves discovering each others' domain IDs, event channels, and sharing memory via grant references.

The LKM supports the usage of an underlying Xen ring buffer. The LKM writes requests to the ring buffer, and reads responses from it. The LKM makes no assumptions about the ordering of responses. The code that interacts with the underlying Xen ring buffer and the Xen event channel is the busiest and most performance-critical part of the system, and is found in mwcomms-xen-iface.c. The sending of an event on the event channel wakes up a thread in the INS, which then reads the next request off the ring buffer (the event indicates that the request is available for consumption). Likewise, the LKM has a special thread that reads responses off the ring buffer(s). It blocks on a semaphore which is up()ed by the LKM's event channel handler.

### Multi-threading

Multithreading support works as follows:

1. A user-mode program writes a request to the LKM's device. The LKM assigns a driver-wide unique ID to that request and associates the caller's PID with the request ID via a connection mapping. Assume that the program indicates it will wait for a response (it is not required to wait unless it says it will do so)..

2. The user-mode program reads a response from the LKM. The LKM will cause that read() to block until it receives the response with an ID that matches the request's ID. As mentioned earlier, the LKM provides a kernel thread that reads responses off the ring buffer and notifies blocked threads when their respective responses have arrived. If the program had indicated that it will not wait, then the LKM destroys the response upon receipt and after some processing. 

This model implies some strict standards:

- The remote side *must* send a response for every request it receives, although the responses can be in a different order than the requests.  

- The programs that use this LKM must be well-written: upon writing a request, they must indicate to the LKM whether or not they will read a response. If the program says it will wait but doesn't, then the LKM will signal a completion variable and leak the response until the LKM is unloaded -- the user-provided thread was supposed to consume the response and destroy it.  The moral of the story is that the user-mode shim should be correct. 

### Leveraging the Linux kernel's VFS

The first iteration of this driver did not leverage the virtual file system (VFS) it presented the shim with pseudo-file objects. This model was mostly adequate until the system was exercised against a more advanced protected application, Apache. Apache makes extensive use of polling, which in turn meant that the shim and LKM had to include support for MagicWand's version of polling. Moreover, there was a design problem wherein the death of a protected application was not always recognized by the LKM, thereby leaking resources.

To alleviate these problems, the LKM was redesigned to leverage VFS and allow the development to focus on MagicWand, rather than re-implementing epoll() and release() support, which had already been done by the Linux community.  With the rewrite, the LKM now backs each Magic Wand socket (mwsocket) with a kernel file object to enable integration with the kernel's VFS. This provides important functionality that programs make use of; in particular:

- release() allows for clean destruction of an mwsocket upon program termination,

- poll() allows for seemless usage of select(), poll() and epoll.

Moreover, mwsockets implement these operations, which are used by the user-mode shim:

- write() for putting a request on the ring buffer

- read() for getting a response off the ring buffer

- ioctl() for modifying the bahavior of an mwsocket, i.e. mimicking the functionality of setsockopt() and fcntl()

The above benefits provided the primary motivation for creating version 0.2 of the LKM. Without integration into the VFS, the shim has to intercept and translate every poll-related call, including the complex epoll structures.

This LKM is structured such that this object, mwcomms-base, serves as the main entry point. It initializes its own kernel device and initializes the Xen subsystem and the netflow channel.

When initializing the Xen subsystem, a callback is passed to it. When a new INS is recognized, the subsystem completes a handshake with it to include sharing a block of memory for a ring buffer. Once that is complete, the callback is invoked. That, in turn, notifies the mwsocket subsystem that there is a new INS available for usage. If there are any listening sockets, those will be "replicated" to the new INS to facilitate multi-INS support. N.B. a new INS is recognized on a XenWatch thread, which we cannot cause to block for a long time. Thus the mwsocket subsystem completes socket replication via a work item.

When a process wants to create a new mwsocket, it sends an IOCTL to the main LKM device. That causes mwcomms-socket to create a new mwsocket, backed by a file object, in the calling process, and return the mwsocket's new file descriptor. Thereafter the process interacts with that file descriptor to perform IO on the mwsocket. Closing the file descriptor will cause the release() callback in mwcomms-socket to be invoked, thus destroying the mwsocket.

The core functionality of this LKM is found in mwcomms-socket.

\newpage
Here's a visualization of the LKM:

```

                             Application-initiated request
                             ----------------------------

/dev/mwcomms
 +----------+
 | open     | <--------------- Init: open /dev/mwcomms
 +----------+
 | release  | <--------------- Shutdown: close /dev/mwcomms
 +----------+
 | ioctl    | <--------------- Check whether FD is for mwsocket, or
 +----------+                   create new mwsocket
      |
      +----------------------------+
                (new mwsocket)     |
                                   |
                                   |
Application-initiated              |         Application/Kernel-initiated
---------------------              v         ----------------------------
                                mwsocket
                               +---------+
send request --------------->  | write   |
                               +---------+
get response for request --->  | read    |
                               +---------+
modify mwsocket behavior --->  | ioctl   |
                               +---------+
                               | poll    | <------ select/poll/epoll
                               +---------+
                               | release | <------ close last ref to underlying 
                               +---------+          file object

```
\newpage

The significant advantages to this design can be seen in the diagram: the kernel facilities are leveraged to support (1) polling and (2) mwsocket destruction, even in the case of process termination. This alleviates the LKM from those burdens. In a future refactor, this could be simplified so that the mwsocket object uses the main device's functions. In this case, a socket() call would result in a direct opening of /dev/mwcomms for a new file descriptor.


### Multi-INS support


To facilitate attack mitigation and TCP/IP stack diversification, the driver supports multiple INSs. This feature is considered in beta; there are still some issues to work out.

The driver does not create new INSs, nor does it direct that to happen. The INSs publish network traffic statistics to XenStore. An external agent (such as mw_distro_ins.py, in the repo) fulfills the task of creating INSs and deciding which INS to use for inbound connections. In turn, the LKM recognizes the appearance of a new INS and completes a handshake with it. Upon a successful handshake, the INS is registered and is considered available for requests.

Once a new INS is registered, any listening sockets must be "replicated" onto it. For instance, say a protected application has requested to listen on 0.0.0.0:80, and INS 1 is doing that on its TCP/IP stack. Then INS 2 appears. To enable multiplexing of the protected application, INS 2 must also listen on that port. The LKM is responsible for telling INS 2 to listen on port 80. It achieves this by creating a work item that crafts the requests (from within the kernel) and sends them to the new INS. It does this via work item because it cannot block the XenStore watching thread.

This behavior means that when an inbound connection on port 80 could come from any known INS. The LKM must direct that connection (a response to an accept) to the thread that originally wrote the accept request, although it may have sent it to a different INS (or set of INSs). The LKM achieves this capability by maintaining a list of inbound connections for each mwsocket. Upon an inbound connection, the response is put in the list and a special semaphore is up()-ed, which the accept()ing thread is blocking on. Moreover, each "replicated" mwsocket has an associated "primary" mwsocket, which is the one exposed to the user.

## Ring Buffer

The ring buffer configuration options are located in the file:
```
common/common_config.h
```
XENEVENT_GRANT_REF_ORDER defines the order of the block of shared memory for the Xen ring buffer. For instance, if the order is 6 and the page size is 4k, we share 2^6 = 64 (0x40) pages, or 262144 (0x40000) bytes. The max we've tried is 8. Xen has a default upper limit on how many pages can be shared (256). That limit can be configured by a Xen boot parameter.

This value was originally 5, but raised to 8 in order to mitigate slowdowns observed when the system was under heavy load.

```
#define XENEVENT_GRANT_REF_ORDER  8 // 256 x 4k pages = 1024k (current default)
```
The ring buffer initialization functions are found in the file mwcomms-xen-iface.c, and is called when a new INS is discovered. by means of the xenbus_watch.

## INS

When an INS is started, 
* Buffer item
* workqueue
* threads


## message_types.h

* mt_response_generic_t
* mt_request_generic_t

