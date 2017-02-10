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
* 01100111 | (     rump ID      )| (sock ID)|
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
#define _MW_SOCKET_PREFIX       (uint8_t) 'C' // 0x43 = 67
#define MW_SOCKET_PREFIX_VAL    (_MW_SOCKET_PREFIX << 24)
#define MW_SOCKET_CLIENT_MASK   0x00ffff00
#define MW_SOCKET_LOCAL_ID_MASK 0x000000ff

// A value is deemed an MW socket fd if it is positive and it has the
// right prefix

#define MW_SOCKET_IS_FD(x)                                      \
    ( ((x) >> MW_SOCKET_PREFIX_SHIFT) == _MW_SOCKET_PREFIX )

#define MW_SOCKET_CREATE(rumpid, sockfd)                                \
    ( MW_SOCKET_PREFIX_VAL | (((uint16_t)rumpid) << 8) | (sockfd) )

#define MW_SOCKET_GET_ID(x)                     \
    ((x) & MW_SOCKET_LOCAL_ID_MASK )

#endif // mwsockets_h
