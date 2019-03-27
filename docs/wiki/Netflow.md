## Netflow
Communication channel and API to the mwcomms driver used to monitor protected application traffic (syscalls), send requests and receive responses. Requests can be used to query the driver for information or to specify an action to be taken, such as a LVDDOS mitigation.

### API
Communication between the mwcomms driver and the monitoring application uses a TCP/IP socket, the address and port are published by the mwcomms driver in XenStore on Dom0 at "/mw/pvm/netflow".

A python library used to interface with Netflow is located at:
* magicwand-commsbackbone/util/mw_netflow.py

A python script used to exercise the Netflow API is located at:
* magicwand-commsbackbone/util/mw_netflow_consumer.py

The core Netflow mwcomms driver files are located at:
* magicwand-commsbackbone/exports/imports/mw_netflow_iface.h
* magicwand-commsbackbone/common/message_types.h
* magicwand-commsbackbone/protvm/kernel/mwcomms/mwcomms-netflow.h
* magicwand-commsbackbone/protvm/kernel/mwcomms/mwcomms-netflow.c
* magicwand-commsbackbone/protvm/kernel/mwcomms/mwcomms-socket.c

### Traffic Information
As soon as a Netflow connection is established with the mwcomms driver all protected application traffic monitoring data is sent to the monitoring application in real time. Multiple connections can be established and traffic data will be sent to every connected client. Traffic is broken into chunks equivalent to syscalls (called observations in the code) and defined by mw_observation_t:

* MwObservationNone    = 0
* MwObservationCreate  = 1
* MwObservationBind    = 2
* MwObservationAccept  = 3
* MwObservationConnect = 4
* MwObservationRecv    = 5
* MwObservationSend    = 6
* MwObservationClose   = 7

Each "observation" message is comprised of a set of data defined by the mw_netflow_info_t structure:

* mw_base_t        base;             // signature: MW_MESSAGE_NETFLOW_INFO
* mw_obs_space_t   obs;              // mw_observation_t
* mw_timestamp_t   ts_session_start; // beginning of session
* mw_timestamp_t   ts_curr;          // time of observation
* mw_socket_fd_t   sockfd;           // Dom-0 unique socket identifier
* mw_endpoint_t    pvm;              // local (PVM) endpoint info
* mw_endpoint_t    remote;           // remote endpoint info
* mw_bytecount_t   bytes_in;         // tot bytes received by the PVM
* mw_bytecount_t   bytes_out;        // tot bytes sent by the PVM
* uint64_t         extra;            // extra data: new sockfd on accept msg

A Request can be sent to the mwcomms driver asking for information or for an action to be executed, these requests are defined by mt_sockfeat_name_val_t:

* MtSockAttribNone
* MtChannelTrafficMonitorOn
* MtChannelTrafficMonitorOff
* MtSockAttribIsOpen
* MtSockAttribOwnerRunning
* MtSockAttribNonblock
* MtSockAttribReuseaddr
* MtSockAttribReuseport
* MtSockAttribKeepalive
* MtSockAttribDeferAccept
* MtSockAttribNodelay
* MtSockAttribSndBuf
* MtSockAttribRcvBuf
* MtSockAttribSndTimeo
* MtSockAttribRcvTimeo
* MtSockAttribSndLoWat
* MtSockAttribRcvLoWat
* MtSockAttribError
* MtSockAttribGlobalCongctl
* MtSockAttribGlobalDelackTicks

### Mitigation
A mitigation request is used to address a possible LVDDOS attack. The following example illustrates how to setup "netcat" as a protected application, make a connection using another instance of "netcat" then request mwcomms to close the connection using the Netflow interface.

* A PVM should be running with the mwcomms driver loaded
* An INS instance should also be running and be connected to the mwcomms driver
* On Dom0 execute the script located in `magicwand-commsbackbone/util/mw_netflow_consumer.py`
* On the PVM start "netcat" in server mode, put the following two lines in a file and make it executable.
    * `export LD_PRELOAD=/home/pvm/ins-production/magicwand-commsbackbone/protvm/user/wrapper/tcp_ip_wrapper.so`
    * `netcat -l 4444`
* Execute the script
* On a different machine execute `netcat [IP] 4444` using the INS instance IP address
* Once connected you can type any message into either netcat and it will be displayed on the other
* In the "mw_netflow_consumer.py" window type the letter `c` which will display open sockets
* To shutdown the netcat open socket, select it from the list, you should see the netcat client terminate
* Shutdown the netcat server with Ctrl-C

To test closing of a socket while data is being transferred use a syntax like the following:
* server: `netcat -l 4444 > outfile`
* client: `netcat [IP] 4444 < infile`
Initiate the mitigation while the file is being transferred.