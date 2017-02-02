#ifndef mwsockets_h
#define mwsockets_h

/*********************************************************************
* Implements support for MagicWand sockets so it is easy to distingush
* between a "normal" file descriptor and a MW socket fd. A MW socket
* fd has a value that is impossible for a Linux kernel, with a
* standard configuration, to issue. It's value is as follows:
*
* Bit (byte)
*   24 (3) |   16 (2) | 8 (1)    | 0 (0)    |
* 76543210 | 76543210 | 76543210 | 76543210 |
* -------------------------------------------
* 01100111 | (rump ID)| (rump-specific fd)  |
*
* The MSB evaluates to 'C' in ascii, its hex value is 0x67.
*
* Note that the mwsock is a 32 bit value that evaluates as a positive
* signed value. The protected VM and each Rump instance muust
* undertand and interpret this value correctly.
*********************************************************************/

typedef int32_t mw_socket_fd_t;


#define MW_SOCKET_PREFIX_MASK   0x7f000000
#define MW_SOCKET_PREFIX_SHIFT  24
#define MW_SOCKET_PREFIX        (uint8_t) 'C'
#define MW_SOCKET_PREFIX_VAL    0x67000000
#define MW_SOCKET_CLIENT_MASK   0x00ff0000
#define MW_SOCKET_LOCAL_FD_MASK 0x0000ffff

// A value is deemed an MW socket fd if it is positive and it has the
// right prefix

#define MW_SOCKET_IS_FD(x)                                              \
    (((int)(x) > 0) &&                                                  \
     (MW_SOCKET_PREFIX_VAL == ( (int32_t)(x) & MW_SOCKET_PREFIX_MASK)) )


#define MW_SOCKET_CREATE(rumpid, sockfd)                                \
    ( MW_SOCKET_PREFIX_VAL | (((uint16_t)rumpid) << 16) | (sockfd) )


#define MW_SOCKET_GET_FD(x)                     \
    ( (x) & MW_SOCKET_LOCAL_FD_MASK )

#define MW_SOCKET_FD_OK(_native)                 \
    ( MW_SOCKET_GET_FD((_native)) == (_native) )

#endif // mwsockets_h
