
Architecture
============
***

NOTES:

A higher level technical description of our part of the system. Describe each sub-system
(ATP, shim, mwcomms, ring buffer, ins, xen, VMs, etc), APIs, interfaces, interactions, maybe some
technical details, this section should set the stage for the detailed design. Include a diagram
that has all the major pieces listed, entry points and APIs and lines indicating communication
and interaction.

***

![INS Architecture diagram](./report/ins_diagram.png)


This section will give an explanation of the major components of the MAGICWAND-commsbackbone in the same order a request would take through the system.

## Shim 

###### Location:
```
protvm/user/wrapper
```
The wrapper directory contains code that is compiled into a shared object and preloaded by the dynamic linker to intercept the following syscalls.  And forward them to the mwcomms driver.


|          |         |          |             |
|:---------|---------|----------|:------------|
| write    | socket  | connect  | getsockopt  |
| read     | bind    | send     | setsockopt  |
| readv    | listen  | sendto   | getsockname |
| writev   | accept  | recv     | getpeername |
| close    | accept4 | recvfrom | fcntl       |
| shutdown |         |          |             |



In order for an application to take advantage of the INS it must be started with the LD_PRELOAD environment variable set to the location of the tcp_ip_wrapper.so shared library.  When that is done, all syscalls pertaining to sockets, listed in the table above will be converted to a format that the mwcomms driver understands and forwarded to the mwcomms driver where it will then be forwarded over the Xen shared memory ring buffer to the INS.

## Mwcomms driver

The mwcomms driver has the logic to negotiate with the INS to create a shared memory ring buffer, and an event channel to signal when messages are available for consumption by the INS, as well as all logic to keep track of when INS's are created or destroyed, and contains all the code for the backing pseudo file system.  This is perhaps the most complex single component of the entire comms-channel.

## Xenstore

Xenstore is a directory structured space to store information between Xen domains.  It is similar to procfs, and it is used in this project to synchronize communications between the INS and the mwcomms LKM.  It stores the grant refs, the ip address of the ins and 

This is an example xenstore configuration between one INS and the PVM:

```

mw = ""
 pvm = ""
  id = "2"
  netflow = "20.60.60.66:49526"
 20 = ""
  ins_dom_id = "20"
  vm_evt_chn_prt = "14"
  gnt_ref_1 = "106c 106d 106e 106f 1070 1071 1072 1073 1074 1075 1076 1077 1078 1079 107a 107b 107c 107d 107e 107f\..."
  gnt_ref_2 = "10ac 10ad 10ae 10af 10b0 10b1 10b2 10b3 10b4 10b5 10b6 10b7 10b8 10b9 10ba 10bb 10bc 10bd 10be 10bf\..."
  gnt_ref_3 = "10ec 10ed 10ee 10ef 10f0 10f1 10f2 10f3 10f4 10f5 10f6 10f7 10f8 10f9 10fa 10fb 10fc 10fd 10fe 10ff\..."
  gnt_ref_4 = "112c 112d 112e 112f 1130 1131 1132 1133 1134 1135 1136 1137 1138 1139 113a 113b 113c 113d 113e 113f\..."
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
     /ins_dom_id="20" contains the domid as a value to extract it easily from the xenstore
     /vm_evt_chn_prt holds the value to establish an event channel between the PVM and the INS
     /gnt_ref_[1..4] contains the indexes into the shared memory pages used for the ring buffer.
     /gnt_ref_chunks contains the number of chunks that are available for reading.
     /vm_evt_chn_is_bound is a boolean flag indicating that the event channel is ready to signal writes to the ring buffer
     /ip_addrs is the list of ip addresses associated with the INS
     /heartbeat is a value that is updated every 1 second to indicate that the INS is still alive
     /network_stats //TODO fill this in

```

## Xen Ring Buffer

The xen ring buffer is a purely xen component, it is composed of pages of memory that, in our case, are given read and write access by both the protected virtual machine and the rumprun unikernel.  It is in this shared memory space that messages are written and consumed by both the INS and the mwcomms driver.  A second component of the ring buffer is the event channel, which is a channel by which the Rumprun unikernel and the mwcomms driver can notify one another that a message has been written to the ring buffer and is ready to be consumed.

## Rumprun Unikernel

The Rumprun unikernel is a general purpose unikernel that has minimally modified the NetBSD rump kernel, to allow running unmodified POSIX code as a unikernel.  As a general purpose unikernel, the Rumprun unikernel was chosen as it had most operating system components already built in, and could allow us to focus on developing the core application infrastructures instead of worrying about writing drivers that already exist in the case that we need them.

## Rumprun Unikernel xe device

The Rumprun unikernel equivalent to the mwcomms driver that interfaces with the ring buffer and XenStore to coordinate the comms-channel initialization as well as pull messages from the ring buffer and send them to the main INS application code.

## xenevent.c

This is where the messages are received from the xe device, converted to their NetBSD equivalents and executed.  Once a command is processed, or a function returned, it is converted back into an MWComms message and sent back down the comms channel to to the originating process.

