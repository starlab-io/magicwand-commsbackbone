# Tasks that need to be done

## Simple vchan clients

1. Start by using node.c and node-select.c from libvchan.
1. Create customized versions to verify controllable notification semantics.
  1. We want to verify that we can write twice to the channel with the
     recipient only getting notified after the second write.
  1. Verify that we can check for available space in the channel, and then do a
     non-blocking write of 2 buffers, with the recipient only being notified
     after the second write.  


## Develop libvchan shim-layer specification

1. Identify the symbols in libxc that are used by libvchan.
1. Identify the hypercall and/xen specific undefined symbols in libxc.
  1. Write a API that defines each of these undefined symbols.


### Implement shim-layer for multiple platforms.

1. Write a Linux-userland implementation of the functions.
  1. This will probably look a whole lot like the implementation from libxc.
1. Write a Linux kernel implementation of the functions.
1. Write a NetBSD rump kernel implementation of the functions.

## Message type library

1. Write message API
  1. Message life-cycle
  1. Message content manipulation
1. Implement message API

## Comm channel operations library

1. Write comm channel API
  1. IO operations use only libvchan functionality
  1. Comm channel life-cycle
  1. Write messages to comm channel
    1. Blocking and non-blocking
  1. Read messages from comm channel
    1. Blocking and non-blocking
  1. Comm channel tuning
    1. Latency and notification
       1. At channel creation time
       1. On a per-message basis

