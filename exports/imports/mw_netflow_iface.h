/*************************************************************************
* STAR LAB PROPRIETARY & CONFIDENTIAL
* Copyright (C) 2016, Star Lab — All Rights Reserved
* Unauthorized copying of this file, via any medium is strictly prohibited.
***************************************************************************/

/******************************************************************************
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
 * 2. Mitigation: a request for mitigation is sent from the analytics
 *    engine to the PVM. The PVM will process the mitigation request
 *    and send a response back to the engine. Example: analytics
 *    engine requests that the PVM close a suspicious socket; the PVM
 *    does so, and reports its status back to the engine. The status
 *    is 0 upon success or a positive Linux errno value on failure.
 *
 * 3. Status: the analytics engine requests information about a
 *    socket, and the PVM issues a response back. For instance, the
 *    engine requests a socket's receive buffer size, the PVM finds
 *    that info and responds with it to the engine.
 *****************************************************************************/

/**
 * Base info for netflow interface. The signature determines the type
 * of message. General classes of messages are:
 *
 */
typedef uint16_t mw_netflow_sig_t;
typedef uint32_t mw_message_id_t;
typedef  int32_t mw_socket_fd_t; // must match mwsocket.h

#define MW_SIG(_x) (0xd300 | (_x))
#define MW_MESSAGE_NETFLOW_INFO            MW_SIG( 0x10 )

#define MW_MESSAGE_SIG_MITIGATION_REQUEST  MW_SIG( 0x20 )
#define MW_MESSAGE_SIG_MITIGATION_RESPONSE MW_SIG( 0x21 )

#define MW_MESSAGE_SIG_STATUS_REQUEST      MW_SIG( 0x30 )
#define MW_MESSAGE_SIG_STATUS_RESPONSE     MW_SIG( 0x31 )

/**
 * Informational message only that come from the PVM. Not for
 * mitigation, not related to status request. Does not provide a
 * message ID since it cannot be responded to.
 */

// Should have space to hold 2 IPv6 address:port pairs plus
// an informational message.
//
// Per http://elixir.free-electrons.com/linux/latest/source/include/linux/inet.h
// #define INET6_ADDRSTRLEN	(48)
//
// So an IPv6:port pair is '[' + 48 bytes + ']:' + 5 bytes = 56 bytes

#define NETFLOW_INFO_MSG_LEN ( 2 * 56 + 30 )

typedef struct _mw_netflow_info
{
    mw_netflow_sig_t sig; // MW_MESSAGE_NETFLOW_INFO
    mw_socket_fd_t   sockfd;
    char             msg[ NETFLOW_INFO_MSG_LEN ];
} mw_netflow_info_t;


/**
 * Specific requests for mitigation or status. These originate from
 * the analytics engine, which is responsible for assigning message ID
 * numbers and tracking outstanding requests until their responses
 * arrive. A response must always have the same message ID number as
 * its associated request.
 */

typedef struct _mw_netflow_base
{
    mw_netflow_sig_t sig;
    mw_message_id_t  id;
} mw_base_t;



/**
 * Mitigation request/response: used when the analytics engine decides
 * to take action against a suspect connection.
 */
typedef enum
{
    MwMitigationNone              = 0x00, // arg: none

    // Per-socket changes
    MwMitigationKillThread        = 0x01, // arg: none
    MwMitigationCloseSocket       = 0x02, // arg: none

    // Send, recv buffer size: arg is byte differential (+/- bytes)
    MwMitigationChangeSockSendBuf = 0x10,
    MwMitigationChangeSockRecvBuf = 0x11,

    // Send, recv low water mark: arg is signed millisec differential (+/- ms)
    MwMitigationChangeSockSendLoWat = 0x12,
    MwMitigationChangeSockRecvLoWat = 0x13,

    // Send, recv timeout: arg is signed millisec differential (+/- ms)
    MwMitigationChangeSockSendTimo = 0x14,
    MwMitigationChangeSockRecvTimo = 0x15,

    // INS-wide changes

    // Change congestion control algo, arg: see vals below
    MwMitigationChangeInsCongctl  = 0x20,

    // Change of delay ACK ticks: arg is signed tick differential
    MwMitigationChangeDelackTicks = 0x21,

} mw_mitigation_action_val_t;
typedef uint32_t mw_mitigation_action_t; // holds mw_mitigation_action_val_t


/**
 * Some of the possible values for the mitigation argument.
 */
typedef enum
{
    MwCongctlReno    = 0x90, // action: congctl
    MwCongctlNewReno = 0x91, // action: congctl
    MwCongctlCubic   = 0x92, // action: congctl
} mw_congctl_arg_val_t;

typedef int32_t mw_mitigation_arg_t; // holds mw_congctl_arg_val_t, among others


/**
 * A mitigation request.
 */
typedef struct _mw_mitigation_request
{
    mw_base_t              base;
    mw_socket_fd_t         sockfd;
    mw_mitigation_action_t act;
    mw_mitigation_arg_t    arg;
} mw_mitigation_request_t;


/**
 * Response to mitigation request. Its id matches that from the
 * associated request.
 */
typedef struct _mw_mitigation_response
{
    mw_base_t base;
    uint32_t  status;
} mw_mitigation_response_t;


/**
 * Request for information about system or socket.
 */
typedef enum
{
    MwInfoSendBuf = 0x10,
    MwInfoRecvBuf = 0x11,

    MwInfoCongctl = 0x20,
} mw_congctl_t;
typedef uint32_t mw_status_t; // holds mw_congctl_t among other things


typedef struct _mw_status_request_msg
{
    mw_base_t   base;
    mw_status_t req;
} mw_status_request_msg_t;


/**
 * Response to info request. Its id matches that from the associated
 * request.
 */
typedef struct _mw_status_response_msg
{
    mw_base_t base;
    uint32_t  val;
} mw_status_response_msg_t;


