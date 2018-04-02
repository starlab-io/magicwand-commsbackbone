/**************************************************************************
 * STAR LAB PROPRIETARY & CONFIDENTIAL
 * Copyright (C) 2016, Star Lab — All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited.
 *************************************************************************/

/**************************************************************************
 * This file describes data that traverses the netflow channel. It
 * defines the message types that can be used on that channel and
 * constitutes an agreement between the MagicWand PVM/INS and the
 * analytics engine. The user of this file must define the uint8_t,
 * uint32_t, etc. Messages here are intended to go over a network
 * connection, so all the integer fields must be in network byte
 * order. The analytics engine may connect to the PVM's netflow
 * channel via TCP/IP. The PVM publishes the IP and port where it is
 * listening in XenStore at /mw/pvm/netflow.
 *
 * Supported message types are:
 *
 * 1. Netflow info: stream of status updates from the PVM to the
 *    analytics engine. The analytics engine consumes these messages
 *    but does not respond to them directly. The info includes a
 *    socket identifier.
 *
 * 2. Feature: a feature request can either read or write a feature. A
 *    feature request is send from the analytics engine to the PVM; a
 *    feature response is then sent from the PVM back to the engine.
 *    Modification of a feature is considered mitigation. The egine
 *    may read the feature first and then write to it, or it might
 *    just write to it without an initial query. For example, the
 *    engine might decide that a socket is suspect and its feature
 *    "receive buffer size" should be changed. It first queries that
 *    feature, calculates what the new value should be, and then sends
 *    a feature write request to change the feature's value. On the
 *    other hand, it might decide that a socket needs to be closed, in
 *    which case it can just send a close request (a feature request
 *    that sets the "open" value to false).
 ************************************************************************/

#ifndef _mw_netflow_iface_h
#define _mw_netflow_iface_h

// Structures are shared across VMs in code built by different gccs.
// Make sure they agree on the layout.
#define MT_STRUCT_ATTRIBS __attribute__ ((__packed__))


/**
 * Base info for netflow interface. The signature determines the type
 * of message (described above).
 */
typedef uint16_t mw_netflow_sig_t;
typedef uint32_t mw_message_id_t;

//
// Every message on the netflow channel must start with this base
//
typedef struct _mw_base
{
    mw_netflow_sig_t sig;
    mw_message_id_t  id;
} __attribute__((packed)) mw_base_t;


#define _MW_SIG_HI 0xd3

#define MW_SIG(_x) ( (_MW_SIG_HI << 8) | (_x) )

#define MW_IS_MESSAGE( _ptr )                                           \
    ( (NULL != (_ptr) )                                                 \
      && ( (ntohs( *(mw_netflow_sig_t *) (_ptr) ) >> 8) == _MW_SIG_HI ) )

#define MW_MESSAGE_SIG_NETFLOW_INFO      MW_SIG( 0x10 )
#define MW_MESSAGE_SIG_FEATURE_REQUEST   MW_SIG( 0x20 )
#define MW_MESSAGE_SIG_FEATURE_RESPONSE  MW_SIG( 0x2f )


/**
 * Informational message only that comes from the PVM. Not for
 * mitigation, not related to status request. Does not provide a
 * message ID since it is not a part of a request/response pair.
 */

// Should have space to hold 2 IPv6 address:port pairs plus
// an informational message.
//
// Per http://elixir.free-electrons.com/linux/latest/source/include/linux/inet.h
// #define INET_ADDRSTRLEN      (16)
// #define INET6_ADDRSTRLEN	(48)
//
// So an IPv6:port pair is '[' + 48 bytes + ']:' + 5 bytes = 56 bytes
//

#define NETFLOW_INFO_ADDR_LEN 16

typedef  int64_t mw_socket_fd_t; // must match mwsocket.h

typedef struct _mw_addr
{
    uint32_t  af; // 4 or 6, wastes space but helps with alignment

    // Address in network order, just as in sockaddr struct
    uint8_t a[ NETFLOW_INFO_ADDR_LEN ];
} __attribute__((packed)) mw_addr_t; // 4 + 16 = 20 bytes


typedef struct _mw_endpoint
{
    mw_addr_t addr;
    uint16_t  port;
} __attribute__((packed)) mw_endpoint_t;


typedef struct _mw_timestamp
{
    uint64_t sec; // seconds
    uint64_t ns;  // nanoseconds
} __attribute__((packed)) mw_timestamp_t;


typedef enum _mw_observation
{
    MwObservationNone    = 0,
    MwObservationCreate  = 1,
    MwObservationBind    = 2,
    MwObservationAccept  = 3,
    MwObservationConnect = 4,
    MwObservationRecv    = 5,
    MwObservationSend    = 6,
    MwObservationClose   = 7,
} mw_observation_t;

typedef uint16_t mw_obs_space_t;
typedef uint64_t mw_bytecount_t;

typedef struct _mw_netflow_info
{
    mw_base_t        base; // signature: MW_MESSAGE_NETFLOW_INFO

    mw_obs_space_t   obs; // mw_observation_t

    mw_timestamp_t   ts_session_start; // beginning of session
    mw_timestamp_t   ts_curr;          // time of observation
    mw_socket_fd_t   sockfd;    // Dom-0 unique socket identifier
    mw_endpoint_t    pvm;       // local (PVM) endpoint info
    mw_endpoint_t    remote;    // remote endpoint info

    mw_bytecount_t   bytes_in;  // tot bytes received by the PVM
    mw_bytecount_t   bytes_out; // tot bytes sent by the PVM

    uint64_t         extra;     // extra data: new sockfd on accept msg
} __attribute__((packed)) mw_netflow_info_t;


/**
 * Specific requests for mitigation or status. These originate from
 * the analytics engine, which is responsible for assigning message ID
 * numbers and tracking outstanding requests until their responses
 * arrive. A response must always have the same message ID number as
 * its associated request.
 */


/**
 * Types and values that are used by both the netflow channel and by
 * the PVM/INS internally. The are prefixed with Mt rather than Mw.
 *
 * Fields passed over the netflow channel are put into network
 * endianess. However, the fields are passed natively interally by the
 * PVM/INS.
 */
typedef enum
{
    MtCongctlReno    = 0x90, // action: congctl
    MtCongctlNewReno = 0x91, // action: congctl
    MtCongctlCubic   = 0x92, // action: congctl
} mt_congctl_arg_val_t;


/**
 * Socket behavioral attributes, normally set via setsockopt() and
 * fcntl(). This subset is supported by Magic Wand. Some of these can
 * also be used for mitigation.
 */

#define MT_SOCK_ATTR_MITIGATES(_x) ((_x) & 0x0100) // mitigation is allowed
#define MT_SOCK_ATTR_PER_INS(_x)   ((_x) & 0x1000) // INS-wide setting

typedef enum
{
    MtSockAttribNone              = 0x0000,

    // Per-socket attributes
    MtSockAttribIsOpen            = 0x0101,
    MtSockAttribOwnerRunning      = 0x0102,

    MtSockAttribNonblock          = 0x0003,
    MtSockAttribReuseaddr         = 0x0004,
    MtSockAttribReuseport         = 0x0005,
    MtSockAttribKeepalive         = 0x0006,
    MtSockAttribDeferAccept       = 0x0007,
    MtSockAttribNodelay           = 0x0008,

    MtSockAttribSndBuf            = 0x0109,
    MtSockAttribRcvBuf            = 0x010a,
    MtSockAttribSndTimeo          = 0x010b,
    MtSockAttribRcvTimeo          = 0x010c,
    MtSockAttribSndLoWat          = 0x010d,
    MtSockAttribRcvLoWat          = 0x010e,
    MtSockAttribError             = 0x010f,

    // INS-wide settings

    // Change congestion control algo, arg: see vals below
    MtSockAttribGlobalCongctl     = 0x1101,
    // Change of delay ACK ticks: arg is signed tick differential
    MtSockAttribGlobalDelackTicks = 0x1102,
} mt_sockfeat_name_val_t;

typedef uint16_t mt_sockfeat_name_t; // holds an mt_sockfeat_name_t


//
// For usage on getsockopt/setsockopt with timeouts
//
typedef struct MT_STRUCT_ATTRIBS _mt_sockfeat_time_arg
{
    uint64_t  s; // seconds
    uint64_t us; // microseconds
} mt_sockfeat_time_arg_t;


typedef union MT_STRUCT_ATTRIBS _mt_sockfeat_arg
{
    uint32_t               v32;
    uint64_t               v64;
    mt_sockfeat_time_arg_t t;
} mt_sockfeat_arg_t;


//
// Valid feature flags
//

//#define MW_FEATURE_FLAG_READ    0x0 // action: read
#define MW_FEATURE_FLAG_WRITE   0x1 // action: write

// Exactly one of the following two bits must be asserted
#define MW_FEATURE_FLAG_BY_SOCK 0x2 // use sockfd as identifier
//#define MW_FEATURE_FLAG_BY_PEER 0x4 // use peer IP as identifier XXXX: UNSUPPORTED

typedef uint16_t mw_sockfeat_flags_t;
typedef uint32_t mw_status_t; // status: 0 on success, otherwise positive Linux errno
//typedef uint64_t mw_feature_arg_t;


/**
 * A feature request. Can be used for getting information (status) or
 * setting value (mitigation).
 *
 * A request that retrieves information must specify a socket
 * identifier (ident.sockfd).
 *
 * (FUTURE) However, a request that sets an attribute may give either
 * a socket identifier or an IP (ident.peer).
 *
 * (FUTURE) If an IP is given, the given attribute will be set on all
 * current and future connections where the remote IP matches the one
 * given.
 *
 */
typedef struct MT_STRUCT_ATTRIBS _mw_feature_request
{
    mw_base_t           base;
    mw_sockfeat_flags_t flags;
    mt_sockfeat_name_t  name;
    mt_sockfeat_arg_t   val; // used only if flags & MW_FEATURE_FLAG_WRITE
    union
    {
        mw_socket_fd_t  sockfd;
        mw_addr_t       remote;
    } ident;
//}  __attribute__((packed)) mw_feature_request_t;
} mw_feature_request_t;

/**
 * Response to mitigation request. Its id matches that from the
 * associated request.
 */
typedef struct _mw_feature_response
{
    mw_base_t         base;
    mw_status_t       status;
    mt_sockfeat_arg_t val; // populated only if !(flags & MW_FEATURE_FLAG_WRITE)
}  __attribute__((packed)) mw_feature_response_t;

#endif // _mw_netflow_iface_h
