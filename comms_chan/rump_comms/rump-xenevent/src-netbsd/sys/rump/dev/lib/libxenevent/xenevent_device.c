// Define a character device

#include <sys/cdefs.h>
//__KERNEL_RCSID(0, "$NetBSD: rndpseudo.c,v 1.35 2015/08/20 14:40:17 christos Exp $");

#if defined(_KERNEL_OPT)
#   include "opt_compat_netbsd.h"
#endif

#include <sys/param.h>
#include <sys/atomic.h>
#include <sys/conf.h>
//#include <sys/cprng.h>
//#include <sys/cpu.h>
#include <sys/evcnt.h>
//#include <sys/fcntl.h>
//#include <sys/file.h>
//#include <sys/filedesc.h>
//#include <sys/ioctl.h>
//#include <sys/kauth.h>
#include <sys/kernel.h>
#include <sys/kmem.h>
#include <sys/kthread.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
//#include <sys/percpu.h>
#include <sys/poll.h>
#include <sys/pool.h>
//#include <sys/proc.h>
//#include <sys/rnd.h>
#include <sys/rndpool.h>
#include <sys/rndsource.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/systm.h> // printf

//#include <sys/vnode.h> 

#include <sys/vfs_syscalls.h>

#include <rump-sys/kern.h>
#include <rump-sys/vfs.h>

#include "xenevent_common.h"
#include "xenevent_device.h"
#include "xen_comms.h"

// Function prototypes 


// src-netbsd/sys/sys/conf.h
static dev_type_open ( xenevent_open  );
static dev_type_close( xenevent_close );
static dev_type_read ( xenevent_read  );
static dev_type_write( xenevent_write );



// Character device entry points 
static struct cdevsw
xenevent_cdevsw = {
    .d_open    = xenevent_open,
    .d_close   = xenevent_close,
    .d_read    = xenevent_read,
    .d_write   = xenevent_write,

    .d_ioctl   = noioctl,
    .d_stop    = nostop,
    .d_tty     = notty,
    .d_poll    = nopoll,
    .d_mmap    = nommap,
    .d_kqfilter = nokqfilter,
    .d_discard = nodiscard,
    .d_flag    = D_OTHER | D_MPSAFE
};



// From
// http://stackoverflow.com/questions/7775991/how-to-get-hexdump-of-a-structure-data
void
hex_dump( const char *desc, void *addr, int len )
{
#ifdef MYDEBUG
    int i;
    unsigned char buff[17];
    unsigned char *pc = (unsigned char*)addr;

    // Output description if given.
    if (desc != NULL)
        printf ("%s:\n", desc);

    if (len == 0) {
        printf("  ZERO LENGTH\n");
        return;
    }
    if (len < 0) {
        printf("  NEGATIVE LENGTH: %i\n",len);
        return;
    }

    // Process every byte in the data.
    for (i = 0; i < len; i++) {
        // Multiple of 16 means new line (with line offset).

        if ((i % 16) == 0) {
            // Just don't print ASCII for the zeroth line.
            if (i != 0)
                printf ("  %s\n", buff);

            // Output the offset.
            printf ("  %04x ", i);
        }

        // Now the hex code for the specific character.
        printf (" %02x", pc[i]);

        // And store a printable ASCII character for later.
        if ((pc[i] < 0x20) || (pc[i] > 0x7e))
            buff[i % 16] = '.';
        else
            buff[i % 16] = pc[i];
        buff[(i % 16) + 1] = '\0';
    }

    // Pad out last line if not exactly 16 characters.
    while ((i % 16) != 0) {
        printf ("   ");
        i++;
    }

    // And print the final ASCII bit.
    printf ("  %s\n", buff);
#endif // MYDEBUG
}


int
xenevent_device_init( void )
{
    int err = 0;
    int  bmaj = -1, cmaj = -1;

    // Attach driver to device
    // See 
    /*error = config_init_component(cfdriver_ioconf_dtv,
		    cfattach_ioconf_dtv, cfdata_ioconf_dtv);
                    if (error)
			return error;
		error = devsw_attach("dtv", NULL, &bmaj, &dtv_cdevsw, &cmaj);
		if (error)
			config_fini_component(cfdriver_ioconf_dtv,
			    cfattach_ioconf_dtv, cfdata_ioconf_dtv);
                            return error; */

    err = devsw_attach( DEVICE_NAME,
                        NULL,
                        &bmaj,
                        &xenevent_cdevsw,
                        &cmaj );
    if ( 0 != err )
    {
        MYASSERT( !"Failed to attach driver" );
        goto ErrorExit;
    }

// Make the character device

    err = rump_vfs_makeonedevnode( S_IFCHR,
                                   DEVICE_PATH,
                                   cmaj,
                                   0 );
    if ( 0 != err )
    {
        MYASSERT( !"Failed to create control device" );
        goto ErrorExit;
    }

    DEBUG_BREAK();

ErrorExit:
    return err;
}

int
xenevent_device_fini( void )
{
    int err = 0;

    return err;
}
    

static int
xenevent_open( dev_t Dev,
               int Flags,
               int Fmt,
               struct lwp * Lwp )
//static int
//xenevent_open(struct cdev *Device __unused,
//              int Flags __unused,
//              int Type __unused,
//              struct lwp *Lwp __unused ) // <<< OR...
              //struct thread *Thread __unused)
{
    int error = 0;
    DEBUG_PRINT("Opened device \"xenevent\" successfully.\n");
    return error;
}

static int
xenevent_close( dev_t Dev,
                int Flags,
                int Fmt,
                struct lwp * Lwp )
//static int
//xenevent_close(struct cdev *Device __unused,
//               int Flag __unused,
//               int Devtype __unused,
//               struct lwp *Lwp __unused ) // <<< OR...
//               // struct thread *td __unused)
{
    DEBUG_PRINT("Closing device \"xenevent\".\n");
    return (0);
}

//
// The read function just takes the buf that was saved via
// xenevent_write() and returns it to userland for accessing.
// uio(9)
//

static int
xenevent_read( dev_t Dev,
               struct uio * Uio,
               int Flag )
//static int
//xenevent_read(struct cdev *Device __unused,
//              struct uio *Uio,
//              int Flag __unused)
{
    int error = 0;

    for ( int i = 0; i < Uio->uio_iovcnt; i++ )
    {
        DEBUG_PRINT( "Read request: %d bytes at %p\n",
                     (int)Uio->uio_iov[i].iov_len, Uio->uio_iov[i].iov_base );
    }
    return error;
}

//
// * xenevent_write takes in a character string and saves it
// * to buf for later accessing.
static int
xenevent_write( dev_t Dev,
                struct uio * Uio,
                int Flag )

//static int
//xenevent_write(struct cdev *Device __unused,
//               struct uio *Uio,
//               int Flag __unused)
{
    int error = 0;

    for ( int i = 0; i < Uio->uio_iovcnt; i++ )
    {
        DEBUG_PRINT( "Write request: %d bytes at %p\n",
                     (int)Uio->uio_iov[i].iov_len, Uio->uio_iov[i].iov_base );
        hex_dump( "Write request",
                  Uio->uio_iov[i].iov_base, (int)Uio->uio_iov[i].iov_len );
    }
    
    DEBUG_BREAK();
    return error;
}
