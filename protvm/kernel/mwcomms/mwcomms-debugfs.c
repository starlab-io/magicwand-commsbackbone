#include "mwcomms-common.h"
#include "mwcomms-debugfs.h"

#include <linux/debugfs.h>


#define MW_DEBUGFS_BUFF_SIZE 2048
#define MW_DEBUGFS_LINE_SIZE 256

// This directory entry will point to `/sys/kernel/debug/mwcomms`.
static struct dentry *mw_debugfs_dir = 0;
static u64 mw_debugfs_tracing_on = 0;
static char mw_debugfs_buffer[ MW_DEBUGFS_BUFF_SIZE ] = { 0 };

mt_dbg_count_t g_mw_debugfs_req_count = { 0 };
mt_dbg_count_t g_mw_debugfs_resp_count = { 0 };


static void
mw_debugfs_buff_append( const char* fmt, ... )
{

    char line[ MW_DEBUGFS_LINE_SIZE ] = { 0 };
    va_list args = { 0 };
    int i, remain = 0;

    va_start( args, fmt );
    i = vsnprintf( line, MW_DEBUGFS_LINE_SIZE - 1, fmt, args );
    va_end( args );

    remain = MW_DEBUGFS_BUFF_SIZE - strnlen( mw_debugfs_buffer, MW_DEBUGFS_BUFF_SIZE ) - 1;
    strncat( mw_debugfs_buffer, line,  remain );

}

static void
mw_debugfs_make_count_buffer( void )
{
    //Clear the buffer
    memset( mw_debugfs_buffer, 0, MW_DEBUGFS_BUFF_SIZE );

    mw_debugfs_buff_append( "\n\n" );
    mw_debugfs_buff_append( "Message Type                   Request        Response\n" );
    mw_debugfs_buff_append( "MtRequestSocketInvalid         %-15lu%-15lu\n",
                        atomic64_read( &g_mw_debugfs_req_count.invalid ),
                        atomic64_read( &g_mw_debugfs_resp_count.invalid ));
    
    mw_debugfs_buff_append( "MtRequestSocketCreate          %-15lu%-15lu\n",
                        atomic64_read( &g_mw_debugfs_req_count.create ),
                        atomic64_read( &g_mw_debugfs_resp_count.create ) );

    mw_debugfs_buff_append( "MtRequestSocketShutdown        %-15lu%-15lu\n",
                        atomic64_read( &g_mw_debugfs_req_count.shutdown ),
                        atomic64_read( &g_mw_debugfs_resp_count.shutdown ) );
    
    mw_debugfs_buff_append( "MtRequestSocketClose           %-15lu%-15lu\n",
                        atomic64_read( &g_mw_debugfs_req_count.close ),
                        atomic64_read( &g_mw_debugfs_resp_count.close ) );
    
    mw_debugfs_buff_append( "MtRequestSocketConnect         %-15lu%-15lu\n",
                        atomic64_read( &g_mw_debugfs_req_count.connect ),
                        atomic64_read( &g_mw_debugfs_resp_count.connect ));
    
    mw_debugfs_buff_append( "MtRequestSocketBind            %-15lu%-15lu\n",
                        atomic64_read( &g_mw_debugfs_req_count.bind ),
                        atomic64_read( &g_mw_debugfs_resp_count.bind ) );
    
    mw_debugfs_buff_append( "MtRequestSocketListen          %-15lu%-15lu\n",
                        atomic64_read( &g_mw_debugfs_req_count.listen ),
                        atomic64_read( &g_mw_debugfs_resp_count.listen ) );
    
    mw_debugfs_buff_append( "MtRequestSocketAccept          %-15lu%-15lu\n",
                        atomic64_read( &g_mw_debugfs_req_count.accept ),
                        atomic64_read( &g_mw_debugfs_resp_count.accept ));
    
    mw_debugfs_buff_append( "MtRequestSocketSend            %-15lu%-15lu\n",
                        atomic64_read( &g_mw_debugfs_req_count.send ),
                        atomic64_read( &g_mw_debugfs_resp_count.accept ) );
    
    mw_debugfs_buff_append( "MtRequestSocketRecv            %-15lu%-15lu\n",
                        atomic64_read( &g_mw_debugfs_req_count.recv ),
                        atomic64_read( &g_mw_debugfs_resp_count.recv ) );

    mw_debugfs_buff_append( "MtRequestSocketRecvFrom        %-15lu%-15lu\n",
                        atomic64_read( &g_mw_debugfs_req_count.recvfrom ),
                        atomic64_read( &g_mw_debugfs_resp_count.recvfrom ) );

    mw_debugfs_buff_append( "MtRequestSocketGetName         %-15lu%-15lu\n",
                        atomic64_read( &g_mw_debugfs_req_count.getname ),
                        atomic64_read( &g_mw_debugfs_resp_count.getname ) );

    mw_debugfs_buff_append( "MtRequestSocketGetPeer         %-15lu%-15lu\n",
                        atomic64_read( &g_mw_debugfs_req_count.getpeer ),
                        atomic64_read( &g_mw_debugfs_resp_count.getpeer ) );
    
    mw_debugfs_buff_append( "MtRequestSocketAttrib          %-15lu%-15lu\n",
                        atomic64_read( &g_mw_debugfs_req_count.attrib ),
                        atomic64_read( &g_mw_debugfs_resp_count.attrib ) );
    
    mw_debugfs_buff_append( "MtRequestSocketPollSetQuery    %-15lu%-15lu\n",
                        atomic64_read( &g_mw_debugfs_req_count.pollsetquery ),
                        atomic64_read( &g_mw_debugfs_resp_count.pollsetquery ) );
    
    mw_debugfs_buff_append( "Unknown                        %-15lu%-15lu\n",
                        atomic64_read( &g_mw_debugfs_req_count.unknown ),
                        atomic64_read( &g_mw_debugfs_resp_count.unknown ) );
    
    mw_debugfs_buff_append( "--------------------------------------------------\n");
    
    mw_debugfs_buff_append( "Total                          %-15lu%-15lu\n",
                        atomic64_read( &g_mw_debugfs_req_count.total ),
                        atomic64_read( &g_mw_debugfs_resp_count.total ) );
    
    mw_debugfs_buff_append( "--------------------------------------------------\n");
    mw_debugfs_buff_append( "--------------------------------------------------\n");

    mw_debugfs_buff_append( "Cumulative total:     %lu\n",
                        atomic64_read( &g_mw_debugfs_req_count.total ) +
                        atomic64_read( &g_mw_debugfs_req_count.total ) );

    mw_debugfs_buff_append( "\n\n" );
}

void mw_debugfs_reset_stats( void )
{

    //requests
    atomic64_set( &g_mw_debugfs_req_count.invalid, 0 );
    atomic64_set( &g_mw_debugfs_req_count.create, 0 );
    atomic64_set( &g_mw_debugfs_req_count.shutdown, 0 );
    atomic64_set( &g_mw_debugfs_req_count.close, 0 );
    atomic64_set( &g_mw_debugfs_req_count.connect, 0 );
    atomic64_set( &g_mw_debugfs_req_count.bind, 0 );
    atomic64_set( &g_mw_debugfs_req_count.listen, 0 );
    atomic64_set( &g_mw_debugfs_req_count.accept, 0 );
    atomic64_set( &g_mw_debugfs_req_count.send, 0 );
    atomic64_set( &g_mw_debugfs_req_count.recv, 0 );
    atomic64_set( &g_mw_debugfs_req_count.recvfrom, 0 );
    atomic64_set( &g_mw_debugfs_req_count.getname, 0 );
    atomic64_set( &g_mw_debugfs_req_count.getpeer, 0 );
    atomic64_set( &g_mw_debugfs_req_count.attrib, 0 );
    atomic64_set( &g_mw_debugfs_req_count.pollsetquery, 0 );
    atomic64_set( &g_mw_debugfs_req_count.unknown, 0 );
    atomic64_set( &g_mw_debugfs_req_count.total, 0 );

    //responses
    atomic64_set( &g_mw_debugfs_resp_count.invalid, 0 );
    atomic64_set( &g_mw_debugfs_resp_count.create, 0 );
    atomic64_set( &g_mw_debugfs_resp_count.shutdown, 0 );
    atomic64_set( &g_mw_debugfs_resp_count.close, 0 );
    atomic64_set( &g_mw_debugfs_resp_count.connect, 0 );
    atomic64_set( &g_mw_debugfs_resp_count.bind, 0 );
    atomic64_set( &g_mw_debugfs_resp_count.listen, 0 );
    atomic64_set( &g_mw_debugfs_resp_count.accept, 0 );
    atomic64_set( &g_mw_debugfs_resp_count.send, 0 );
    atomic64_set( &g_mw_debugfs_resp_count.recv, 0 );
    atomic64_set( &g_mw_debugfs_resp_count.recvfrom, 0 );
    atomic64_set( &g_mw_debugfs_resp_count.getname, 0 );
    atomic64_set( &g_mw_debugfs_resp_count.getpeer, 0 );
    atomic64_set( &g_mw_debugfs_resp_count.attrib, 0 );
    atomic64_set( &g_mw_debugfs_resp_count.pollsetquery, 0 );
    atomic64_set( &g_mw_debugfs_resp_count.unknown, 0 );
    atomic64_set( &g_mw_debugfs_resp_count.total, 0 );
    
}

static ssize_t mw_debugfs_buffer_reader( struct file *fp, char __user *user_buffer,
                                        size_t count, loff_t *position )
{

    mw_debugfs_make_count_buffer();

    return simple_read_from_buffer( user_buffer,
                                    count,
                                    position,
                                    &mw_debugfs_buffer,
                                    MW_DEBUGFS_BUFF_SIZE );
}

static ssize_t mw_debugfs_reset( struct file *fp, const char __user *user_buffer,
                                         size_t count, loff_t *position )
{
    mw_debugfs_reset_stats();

    return count;
}

static const struct file_operations mw_debugfs_message_counts_fops =
{
    .read = mw_debugfs_buffer_reader,
};

static const struct file_operations mw_debugfs_reset_fops =
{
    .write = mw_debugfs_reset
};

void mw_debugfs_init()
{
    
    int rc = 0;
    struct dentry *mw_tmp_dir;
    int filevalue = 0;
    
    mw_debugfs_dir = debugfs_create_dir(DEVICE_NAME, 0);
    if (!mw_debugfs_dir) {
        pr_err( "Failed to create debugfs directory /sys/kernel/debug/%s\n",
                DEVICE_NAME);
        rc = -1;
        goto ErrorExit;
    }

    mw_tmp_dir = debugfs_create_file("message_counts",
                                     0644,
                                     mw_debugfs_dir,
                                     &filevalue,
                                     &mw_debugfs_message_counts_fops);
    if( ! mw_tmp_dir )
    {
        printk("Could not create text file\n");
        goto ErrorExit;
    }

    mw_tmp_dir = debugfs_create_file("reset",
                                     0244,
                                     mw_debugfs_dir,
                                     &filevalue,
                                     &mw_debugfs_reset_fops);
    if( ! mw_tmp_dir )
    {
        printk("Could not create text file\n");
        goto ErrorExit;
    }


    mw_tmp_dir = debugfs_create_u64("tracing_on",
                                    0644,
                                    mw_debugfs_dir,
                                    &mw_debugfs_tracing_on );
    if( ! mw_tmp_dir )
    {
        printk( "Could not create tracing_on debugfs directory\n" );
        goto ErrorExit;
    }
    
    pr_info( "debugfs created at /sys/kernel/debug/%s\n", DEVICE_NAME );

ErrorExit:
    return;
}



void mw_debugfs_request_count( mt_request_generic_t* Request )
{

    if( ! mw_debugfs_tracing_on )
    {
        goto ErrorExit;
    }

    switch ( Request->base.type )
    {
    case MtRequestInvalid:
        atomic64_inc( &g_mw_debugfs_req_count.invalid );
        atomic64_inc( &g_mw_debugfs_req_count.total );
        break;
    case MtRequestSocketCreate:
        atomic64_inc( &g_mw_debugfs_req_count.create );
        atomic64_inc( &g_mw_debugfs_req_count.total );
        break;
    case MtRequestSocketShutdown:
        atomic64_inc( &g_mw_debugfs_req_count.shutdown );
        atomic64_inc( &g_mw_debugfs_req_count.total );
        break;
    case MtRequestSocketClose:
        atomic64_inc( &g_mw_debugfs_req_count.close );
        atomic64_inc( &g_mw_debugfs_req_count.total );
        break;
    case MtRequestSocketConnect:
        atomic64_inc( &g_mw_debugfs_req_count.connect );
        atomic64_inc( &g_mw_debugfs_req_count.total );
        break;
    case MtRequestSocketBind:
        atomic64_inc( &g_mw_debugfs_req_count.bind );
        atomic64_inc( &g_mw_debugfs_req_count.total );
        break;
    case MtRequestSocketListen:
        atomic64_inc( &g_mw_debugfs_req_count.listen );
        atomic64_inc( &g_mw_debugfs_req_count.total );
        break;
    case MtRequestSocketAccept:
        atomic64_inc( &g_mw_debugfs_req_count.accept );
        atomic64_inc( &g_mw_debugfs_req_count.total );
        break;
    case MtRequestSocketSend:
        atomic64_inc( &g_mw_debugfs_req_count.send );
        atomic64_inc( &g_mw_debugfs_req_count.total );
        break;
    case MtRequestSocketRecv:
        atomic64_inc( &g_mw_debugfs_req_count.recv );
        atomic64_inc( &g_mw_debugfs_req_count.total );
        break;
    case MtRequestSocketRecvFrom:
        atomic64_inc( &g_mw_debugfs_req_count.recvfrom );
        atomic64_inc( &g_mw_debugfs_req_count.total );
        break;
    case MtRequestSocketGetName:
        atomic64_inc( &g_mw_debugfs_req_count.getname );
        atomic64_inc( &g_mw_debugfs_req_count.total );
        break;
    case MtRequestSocketGetPeer:
        atomic64_inc( &g_mw_debugfs_req_count.getpeer );
        atomic64_inc( &g_mw_debugfs_req_count.total );
        break;
    case MtRequestSocketAttrib:
        atomic64_inc( &g_mw_debugfs_req_count.attrib );
        atomic64_inc( &g_mw_debugfs_req_count.total );
        break;
    case MtRequestPollsetQuery:
        atomic64_inc( &g_mw_debugfs_req_count.pollsetquery );
        atomic64_inc( &g_mw_debugfs_req_count.total );
        break;
    default:
        atomic64_inc( &g_mw_debugfs_req_count.total );
        atomic64_inc( &g_mw_debugfs_req_count.unknown );
    }

ErrorExit:
    
    return;
}

void mw_debugfs_response_count( mt_response_generic_t* Response )
{
    if( ! mw_debugfs_tracing_on )
    {
        goto ErrorExit;
    }

    switch ( Response->base.type )
    {
    case MtResponseInvalid:
        atomic64_inc( &g_mw_debugfs_resp_count.invalid );
        atomic64_inc( &g_mw_debugfs_resp_count.total );
        break;
    case MtResponseSocketCreate:
        atomic64_inc( &g_mw_debugfs_resp_count.create );
        atomic64_inc( &g_mw_debugfs_resp_count.total );
        break;
    case MtResponseSocketShutdown:
        atomic64_inc( &g_mw_debugfs_resp_count.shutdown );
        atomic64_inc( &g_mw_debugfs_resp_count.total );
        break;
    case MtResponseSocketClose:
        atomic64_inc( &g_mw_debugfs_resp_count.close );
        atomic64_inc( &g_mw_debugfs_resp_count.total );
        break;
    case MtResponseSocketConnect:
        atomic64_inc( &g_mw_debugfs_resp_count.connect );
        atomic64_inc( &g_mw_debugfs_resp_count.total );
        break;
    case MtResponseSocketBind:
        atomic64_inc( &g_mw_debugfs_resp_count.bind );
        atomic64_inc( &g_mw_debugfs_resp_count.total );
        break;
    case MtResponseSocketListen:
        atomic64_inc( &g_mw_debugfs_resp_count.listen );
        atomic64_inc( &g_mw_debugfs_resp_count.total );
        break;
    case MtResponseSocketAccept:
        atomic64_inc( &g_mw_debugfs_resp_count.accept );
        atomic64_inc( &g_mw_debugfs_resp_count.total );
        break;
    case MtResponseSocketSend:
        atomic64_inc( &g_mw_debugfs_resp_count.send );
        atomic64_inc( &g_mw_debugfs_resp_count.total );
        break;
    case MtResponseSocketRecv:
        atomic64_inc( &g_mw_debugfs_resp_count.recv );
        atomic64_inc( &g_mw_debugfs_resp_count.total );
        break;
    case MtResponseSocketRecvFrom:
        atomic64_inc( &g_mw_debugfs_resp_count.recvfrom );
        atomic64_inc( &g_mw_debugfs_resp_count.total );
        break;
    case MtResponseSocketGetName:
        atomic64_inc( &g_mw_debugfs_resp_count.getname );
        atomic64_inc( &g_mw_debugfs_resp_count.total );
        break;
    case MtResponseSocketGetPeer:
        atomic64_inc( &g_mw_debugfs_resp_count.getpeer );
        atomic64_inc( &g_mw_debugfs_resp_count.total );
        break;
    case MtResponseSocketAttrib:
        atomic64_inc( &g_mw_debugfs_resp_count.attrib );
        atomic64_inc( &g_mw_debugfs_resp_count.total );
        break;
    case MtResponsePollsetQuery:
        atomic64_inc( &g_mw_debugfs_resp_count.pollsetquery );
        atomic64_inc( &g_mw_debugfs_resp_count.total );
        break;
    default:
        atomic64_inc( &g_mw_debugfs_resp_count.total );
        atomic64_inc( &g_mw_debugfs_resp_count.unknown );
    }

ErrorExit:
    
    return;
}

void mw_debugfs_fini()
{
    debugfs_remove_recursive( mw_debugfs_dir );
}
