#ifndef mwcomms_ioctls_h
#define mwcomms_ioctls_h

///
/// Defines IOCTLs so that an mwcomm shim can interact with the
/// driver. These ioctls() are sent to the mwcomm device and not to
/// sockets that the driver creates.
///

#ifdef __KERNEL__
#  include <linux/ioctl.h>
#else
#  include <sys/ioctl.h>
#endif // __KERNEL__

#include <message_types.h>

/******************************************************************************
 * Misc common definitions
 ******************************************************************************/
#define MW_ERROR_REMOTE_SURPRISE_CLOSE EPIPE


/******************************************************************************
 * IOCTL codes and structures for the mwcomms device itself
 ******************************************************************************/

// IOCTLs to mwcomms device itself
#define MW_IOCTL_MAGIC   'M'
#define MW_IOCTL_SEQBASE   5

typedef int  mwsocket_t;

typedef struct _mwsocket_create_args
{
    mwsocket_t outfd; // OUT
    int domain;       // IN
    int type;         // IN
    int protocol;     // IN
} mwsocket_create_args_t;

typedef struct _mwsocket_verify_args
{
    int  fd;              // IN
    bool is_mwsocket;     // OUT
} mwsocket_verify_args_t;

//
// Create a new MWSOCKET
//
#define MW_IOCTL_CREATE_SOCKET                  \
    _IOWR( MW_IOCTL_MAGIC, MW_IOCTL_SEQBASE+0, mwsocket_create_args_t * )


//
// Check: is the given FD for an MW socket?
//
#define MW_IOCTL_IS_MWSOCKET                    \
    _IOWR( MW_IOCTL_MAGIC, MW_IOCTL_SEQBASE+1, mwsocket_verify_args_t * )



/******************************************************************************
 * IOCTL codes and structures for the mwcomms device itself
 ******************************************************************************/

#define MW_SOCKET_IOCTL_MAGIC 'S'
#define MW_SOCKET_IOCTL_SEQBASE 30

//
// For notifying mwsocket that its flags are updated,
// e.g. fcntl(mwfd, F_SETFD, flags ) is being called.
//

typedef struct _mwsocket_attrib
{
    // IN: true=set, false=get
    bool          modify;

    // IN: from message_types.h
    mt_socket_attrib_t attrib;

    // IN/OUT: value
    unsigned long value;
} mwsocket_attrib_t;

#define MW_IOCTL_SOCKET_ATTRIBUTES                      \
    _IOWR( MW_SOCKET_IOCTL_MAGIC, MW_IOCTL_SEQBASE+1, mwsocket_attrib_t * )


#endif // mwcomms_ioctls_h
