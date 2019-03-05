
Design
======

***
NOTES

A lower level, technical section that describes the details about the design implemented.
Maybe a bulleted list of each major sub-system along with a paragraph or two describing
implementation details including things like programming language, how the sub-system works,
why we chose that solution over other possible solutions, performance notes, special features,
debugging, security, etc. For example the INS, why did we choose Rump, what are the pros and
cons, why didn't we choose a linux OS or containers, etc. Include any interesting implementation
details or diagrams.

***


### Shim


Several different implementations of the send function were designed, and the most performant version was determined to be the batch send method.  By default the mwcomms system would only 



### Mwcomms driver

* Pseudo filesystem in mwcomms driver
    * how it enables asynchronous updates of socket poll flags    

* Polling

* Data Structures
    * mwsocket_instance_t
    * mwsocket_active_request_t
    * g_mwsocket_state
    
* Multi-INS
    * Socket replication
    * socket state propagation

### Ring Buffer

* size/slots

### INS

* Buffer item
* workqueue
* threads


### message_types.h

* mt_response_generic_t
* mt_request_generic_t

