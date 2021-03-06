\newpage

# Architecture Diagram

![MwComms Isolated Network Channel Architecture Diagram](./media/image2.png)


# Introduction

The MwComms isolated network channel is designed to intercept all network traffic from a process in a Xen virtual domain.  The network traffic is intercepted by a pre-loaded shared library that implements the system calls used to create and interact with sockets.  The data from those calls is then processed and forwarded to the /dev/mwcomms kernel module that then forwards the information to a Rumprun unikernel that behaves as the network stack for the protected process.  The communication between the mwcomms kernel device and the Rumprun unikernel is done through a high speed shared memory channel that Xen provides to facilitate communication between domains.

# Shim

```
Location:
protvm/user/wrapper
```

The shim component of the MwComms isolated network channel is a shared library that is preemptively loaded via the LD_PRELOAD environment variable.  Using the LD_PRELOAD environment variable enables the shim to overwrite the functionality of all system calls that create or interact with TCP/IP sockets.  Table 1 contains a list of functions implemented by the shim.

|          |         |          |             |
|----------|---------|----------|-------------|
| write    | socket  | connect  | getsockopt  |
| read     | bind    | send     | setsockopt  |
| readv    | listen  | sendto   | getsockname |
| writev   | accept  | recv     | getpeername |
| close    | accept4 | recvfrom | fcntl       |
| shutdown |         |          |             |
    
Table 1 glibc functions intercepted by the shim

It may be noted that the functions listed in Table 1 do not perform operations solely on IPv4 sockets.  Functions such as write, read, close, etc. also perform options on file descriptors so the shim must be able to pass these calls to the operating system. This is accomplished through use of the ```__attribute__((constructor))``` function attribute to specify an initialization function that must run before main is called on the protected process. The constructor function ```init_wrapper()``` stores the addresses of the functions listed in Table 1 as global function pointers so system calls can still be forwarded to glibc if they perform operations on any socket that is not an MwSocket.

Whenever the protected application creates a new IPv4 socket the shim intercepts the call to ```socket()``` and forwards it to the mwcomms kernel module via ioctl.  Once the kernel module recieves the ioctl a new mwsocket file object is created using the Linux virtual file system and the appropriate file descriptor is returned.  After the socket is created, system calls like bind, listen, etc. are converted to the mt_request_generic_t type and written to the mwcomms kernel driver.

# MwSocket File Descriptor

```
Location:
common/mwsocket.h
```

Implements support for MagicWand sockets so it is easy to distingush
between a "normal" file descriptor and a MW socket fd. A MW socket
fd has a value that is impossible for a Linux kernel, with a
standard configuration, to issue. It's value is as follows:

```
Bit (byte)
   56(7) |    48(6) |    40(5) |    32(4) |   24(3) |   16(2) |    8(1) |    0(0) |
76543210 | 76543210 | 76543210 | 76543210 | 7654321 | 7654321 | 7654321 | 7654321 |
-----------------------------------------------------------------------------------
01010011 | (     dom ID      ) | (unused) |     (            sock ID        )     |

The MSB evaluates to 'S' in ASCII, its hex value is 0x53.
```

Note that the mwsock is a 64 bit value that evaluates as a positive
signed value. The protected VM and each Rump instance must
understand and interpret this value correctly.


# MwComms Driver

```
Location:
/protvm/kernel/mwcomms
```

The MagicWand driver (Linux kernel module, or LKM) for the protected virtual machine (PVM) facilitates the passing of requests from a protected application to one or more Isolated Network Stacks (INSs, currently backed by Rump unikernels on the same hardware). It also facilitates the delivery of a response, produced by an INS, to the protected application that expects it. This LKM is intended to be exercised by a custom shared object ("shim") that the protected application loads via LD_PRELOAD.

The LKM supports multithreading / multiprocessing - e.g a multi-threaded application above it can read/write to its device, and each request (write) can expect to receive the corresponding response (via read). It is designed to be fast, with expected slowdowns in the 10s of milliseconds relative to native speeds.

The LKM supports a handshake with another Xen virtual machine, the INS unikernel agent (implemented on the Rump unikernel). The handshake involves discovering each others' domain IDs, event channels, and sharing memory via grant references.

The LKM supports the usage of an underlying Xen ring buffer. The LKM writes requests to the ring buffer, and reads responses from it. The LKM makes no assumptions about the ordering of responses. The code that interacts with the underlying Xen ring buffer and the Xen event channel is the busiest and most performance-critical part of the system, and is found in mwcomms-xen-iface.c. The sending of an event on the event channel wakes up a thread in the INS, which then reads the next request off the ring buffer (the event indicates that the request is available for consumption). Likewise, the LKM has a special thread that reads responses off the ring buffer(s). It blocks on a semaphore which is up()ed by the LKM's event channel handler.

## Multi-threading

Multithreading support works as follows:

1. A user-mode program writes a request to the LKM's device. The LKM assigns a driver-wide unique ID to that request and associates the caller's PID with the request ID via a connection mapping. Assume that the program indicates it will wait for a response (it is not required to wait unless it says it will do so)..

2. The user-mode program reads a response from the LKM. The LKM will cause that read() to block until it receives the response with an ID that matches the request's ID. As mentioned earlier, the LKM provides a kernel thread that reads responses off the ring buffer and notifies blocked threads when their respective responses have arrived. If the program had indicated that it will not wait, then the LKM destroys the response upon receipt and after some processing. 

This model implies some strict standards:

- The remote side *must* send a response for every request it receives, although the responses can be in a different order than the requests.  

- The programs that use this LKM must be well-written: upon writing a request, they must indicate to the LKM whether or not they will read a response. If the program says it will wait but doesn't, then the LKM will signal a completion variable and leak the response until the LKM is unloaded -- the user-provided thread was supposed to consume the response and destroy it.  The moral of the story is that the user-mode shim should be correct. 

## Leveraging the Linux kernel's VFS

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

## Multi-INS support

To facilitate attack mitigation and TCP/IP stack diversification, the driver supports multiple INSs. This feature is considered in beta; there are still some issues to work out.

The driver does not create new INSs, nor does it direct that to happen. The INSs publish network traffic statistics to XenStore. An external agent (such as mw_distro_ins.py, in the repo) fulfills the task of creating INSs and deciding which INS to use for inbound connections. In turn, the LKM recognizes the appearance of a new INS and completes a handshake with it. Upon a successful handshake, the INS is registered and is considered available for requests.

Once a new INS is registered, any listening sockets must be "replicated" onto it. For instance, say a protected application has requested to listen on 0.0.0.0:80, and INS 1 is doing that on its TCP/IP stack. Then INS 2 appears. To enable multiplexing of the protected application, INS 2 must also listen on that port. The LKM is responsible for telling INS 2 to listen on port 80. It achieves this by creating a work item that crafts the requests (from within the kernel) and sends them to the new INS. It does this via work item because it cannot block the XenStore watching thread.

This behavior means that when an inbound connection on port 80 could come from any known INS. The LKM must direct that connection (a response to an accept) to the thread that originally wrote the accept request, although it may have sent it to a different INS (or set of INSs). The LKM achieves this capability by maintaining a list of inbound connections for each mwsocket. Upon an inbound connection, the response is put in the list and a special semaphore is up()-ed, which the accept()ing thread is blocking on. Moreover, each "replicated" mwsocket has an associated "primary" mwsocket, which is the one exposed to the user.

# Netflow

```
Kernel component location:
protvm/kernel/mwcomms/mwcomms-netflow.c

Interface:
exports/imports/mw_netflow_iface.h

API component:
util/mw_netflow.py
util/mw_netflow_consumer.py
```
The netflow component of the MwComms network isolation channel is an open port created in the function ``` mw_netflow_init_listen_port( void ) ``` in the mwcomms-netflow.c source file.  This port is able to receive connection from the netflow library defined in mw_netflow.py.  The netflow port is published to the xenstore directory /mw/pvm/netflow which lists the ip address of the listening interface and port assigned.


## Messaging Format

```
location:
common/message_types.h
```

The data structures that define the format for data passing over the ring buffer are located in message_types.h.  The basic structure of a mwcomms message is a struct defined for each unique message type that needs to be passed over the ring buffer.  These message types typically take the form of a mt_request_base_t fields with additional fields for message specific data.  What is actually written to the ring buffer is data of the type mt_request_generic_t which is a union of all unique message types.

Once a message is received by either the mwcomms driver or the INS over the ring buffer, a field is checked in the mw_request_base_t field common to all structs in union, and is 


# XenStore

The xenstore is used by MwComms to publish data needed to coordinate the different parts of the system.  This includes grant refs for the ring buffer, and ip address/port information for the netflow and front-end APIs.

This is an example xenstore configuration between one INS and the PVM:

```
mw = ""
 pvm = ""
  id = "2"
  netflow = "20.60.60.66:49526"
 20 = ""
  ins_dom_id = "20"
  vm_evt_chn_prt = "14"
  gnt_ref_1 = "106c 106d 106e 106f 1070 1071 1072 1073 1074 1075 1076 1077 1078...
  gnt_ref_2 = "10ac 10ad 10ae 10af 10b0 10b1 10b2 10b3 10b4 10b5 10b6 10b7 10b8...
  gnt_ref_3 = "10ec 10ed 10ee 10ef 10f0 10f1 10f2 10f3 10f4 10f5 10f6 10f7 10f8...
  gnt_ref_4 = "112c 112d 112e 112f 1130 1131 1132 1133 1134 1135 1136 1137 1138...
  gnt_ref_chunks = "4"
  vm_evt_chn_is_bound = "1"
  ip_addrs = " 20.60.60.138 127.0.0.1"
  heartbeat = "1807561"
  network_stats = "1f4:0:0:0"
```


```
mw/pvm/id holds the domain id of the PVM
mw/pvm/netflow holds the ip address and the port used to connect to the mwcomms LKM netflow channel
mw/20 is the root directory for the INS
ins_dom_id="20" contains the domid as a value to extract it easily from the xenstore
vm_evt_chn_prt  holds the value to establish an event channel between the PVM and the INS
gnt_ref_[1..4] contains the indexes into the shared memory pages used for the ring buffer.
gnt_ref_chunks contains the number of chunks that are available for reading.
vm_evt_chn_is_bound is a boolean flag indicating that the event channel is ready to signal writes to the ring buffer
ip_addrs is the list of ip addresses associated with the INS
heartbeat is a value that is updated every 1 second to indicate that the INS is still alive
```
     
# INS

## Command Dispatcher


```
Location:
ins/ins-rump/apps/ins-app/xenevent.c
```

Application for Rump userspace that manages commands from the protected virtual machine (PVM) over xen shared memory, as well as the associated network connections. The application is designed to minimize dynamic memory allocations after startup and to handle multiple blocking network operations simultaneously. 

This application processes incoming requests as follows:

1. The dispatcher function yields its own thread to allow worker threads to process and free up their buffers. We do this because Rump uses a non-preemptive scheduler. 

2. The dispatcher function selects an available buffer from the  buffer pool and reads an incoming request into it.

3. The dispatcher examines the request:

    a. If the request is for a new socket request, it selects an available thread for the socket.

    b. Otherwise, the request is for an existing connection. It finds the thread that is handling the connection and assigns the request to that thread.

A request is assigned to a thread by placing its associated buffer index into the thread's work queue and signalling to the thread that work is available via a semaphore.

4. Worker threads are initialized on startup and block on a semaphore until work becomes available. When there's work to do, the thread is given control and processes the oldest request in its queue against the socket file descriptor it was assigned. In case the request is for a new socket, it is processed immediately by the dispatcher thread so subsequent requests against that socket can find the thread that handled it.

The primary functions for a developer to understand are:

1. worker_thread_func:
One runs per worker thread. It waits for requests processes them against the socket that the worker thread is assigned.

2. message_dispatcher:
Finds an available buffer and reads a request into it. Finds the thread that will process the request, and, if applicable, adds the request to its work queue and signals to it that work is there.


## INS device driver /dev/xe

```
Location:
ins/ins-rump/src-netbsd/sys/rump/dev/lib/libxenevent/xenevent.c
```
Rumprun unikernel driver that interfaces with the ring buffer and xen components.

## NPF Library

```
Location:
ins/ins-rump/apps/lib/npfctl
```

In order to facilitate ip block mitigations, the npfctl command line utility was modified into a library.  The build steps are located in the Makefile in the npfctl directory listed above.

# Netflow Interface

```
Location:
util/mw_netflow.py
util/mw_netflow_consumer.py
```

The netflow interface can be used in two ways: 

1. importing mw_netflow.py into an existing project 
2. using the supplied command line interface mw_netflow_consumer.py

