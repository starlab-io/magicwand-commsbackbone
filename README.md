# Communication Backbone For MAGICWAND

The comms backbone for MAGICWAND is used by the various MAGICWAND
components for efficient communication between programs running in
different Xen domains.  The comm backbone is built in Xen vchan, a
high-speed, high-bandwidth bi-directional communication channel based on
shared-memory ring buffers.  Users of the backbone find each other via a
rendezvous protocol built on top of XenStore.

# Message Oriented

The comm backbone is message oriented.  Each message is TLV-formtted (Type,
Length, Value), with the Type and Length set in a fixed-size message header
and the Length refers to the length of the Value, not the total message
length.

## Message Size Limit

The maximum total message size (header + Value) is 1 MB.  With this limit,
a message can be written without blocking into an empty, maximally sized
vchan ring buffer.  The maximum ring buffer size is imposed by Xen's
libvchan and is related to the maximum grant table size.  NOTE:  I did not
dig too deeply into the grant table size limit code to determine what the
consequences are if one creates a vchan with a maximally sized ring buffer.


## Predefined Message Types

The comm backbone predefines a number of message types and includes helper
functions to simplify writing and reading these message types.


# Rendezvous Protocol

The Rendezvous Protocol uses well-defined, service-specific, paths in
XenStore in order for "clients" to find "servers".  The Rendezvous Protocol
must be bootstrapped by a service running in Dom0.

