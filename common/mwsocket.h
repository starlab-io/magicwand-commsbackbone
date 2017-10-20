#ifndef mwsockets_h
#define mwsockets_h

/*********************************************************************
 * Implements support for MagicWand sockets so it is easy to distingush
 * between a "normal" file descriptor and a MW socket fd. A MW socket
 * fd has a value that is impossible for a Linux kernel, with a
 * standard configuration, to issue. It's value is as follows:
 *
 * Bit (byte)
 *    56(7) |    48(6) |    40(5) |    32(4) |   24(3) |   16(2) |    8(1) |    0(0) |
 * 76543210 | 76543210 | 76543210 | 76543210 | 7654321 | 7654321 | 7654321 | 7654321 |
 * -----------------------------------------------------------------------------------
 * 01010011 | (     rump ID     ) | xxxxxxxx |     (            sock ID        )     |
 *
 * The MSB evaluates to 'S' in ASCII, its hex value is 0x53.
 *
 * Note that the mwsock is a 64 bit value that evaluates as a positive
 * signed value. The protected VM and each Rump instance muust
 * undertand and interpret this value correctly.
 *********************************************************************/

typedef int64_t mw_socket_fd_t; // signed
typedef  mw_socket_fd_t mw_fd_t; // alias

#define MW_SOCKET_PREFIX_MASK   0x7f00000000000000
#define MW_SOCKET_PREFIX_SHIFT  56
#define _MW_SOCKET_PREFIX       (uint64_t) 'S' // 0x53 = 0n83
#define MW_SOCKET_PREFIX_VAL    (((uint64_t)_MW_SOCKET_PREFIX) << MW_SOCKET_PREFIX_SHIFT)


#define MW_SOCKET_CLIENT_SHIFT  40
#define MW_SOCKET_CLIENT_MASK   0x00ffff0000000000

#define MW_SOCKET_LOCAL_ID_MASK 0x00000000ffffffff

// A value is deemed an MW socket fd if it is positive and it has the
// right prefix

#define MW_SOCKET_IS_FD(x)                                      \
    ( (((uint64_t)x) >> MW_SOCKET_PREFIX_SHIFT) == _MW_SOCKET_PREFIX )

#define MW_SOCKET_CREATE(clientid, sockfd)                              \
    (uint64_t) ( MW_SOCKET_PREFIX_VAL |                                 \
                 ( ((uint64_t)clientid) << MW_SOCKET_CLIENT_SHIFT ) |   \
                 (sockfd) )

#define MW_SOCKET_GET_ID(x)                                     \
    ((x) & MW_SOCKET_LOCAL_ID_MASK )

#endif // mwsockets_h
