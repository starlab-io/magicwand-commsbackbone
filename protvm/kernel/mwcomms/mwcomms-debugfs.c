
#include "mwcomms-common.h"
#include "mwcomms-debugfs.h"

#include <linux/debugfs.h>

// This directory entry will point to `/sys/kernel/debug/mwcomms`.
static struct dentry *mw_debugfs_dir = 0;
mt_dbg_count_t g_mw_dbg_req_count = { 0 };
mt_dbg_count_t g_mw_dbg_resp_count = { 0 };
u64 mw_tracing_on = 0;

#define MW_TRACE_BUFF_SIZE 2048
#define MW_TRACE_LINE_SIZE 256
char mw_trace_buffer[ MW_TRACE_BUFF_SIZE ] = "This is a test of the emergency broadcast system\n";


void mw_dbg_buff_append( const char* fmt, ... )
{

    char line[ MW_TRACE_LINE_SIZE ] = { 0 };
    va_list args = { 0 };
    int i, remain = 0;

    va_start( args, fmt );
    i = vsnprintf( line, MW_TRACE_LINE_SIZE - 1, fmt, args );
    va_end( args );

    remain = MW_TRACE_BUFF_SIZE - strnlen( mw_trace_buffer, MW_TRACE_BUFF_SIZE ) - 1;
    strncat( mw_trace_buffer, line,  remain );

}

void make_count_buffer( void )
{
    //Clear the buffer
    memset( mw_trace_buffer, 0, MW_TRACE_BUFF_SIZE );

    mw_dbg_buff_append( "\n\n" );
    mw_dbg_buff_append( "Message Type                Request\t\tResponse\n" );
    mw_dbg_buff_append( "MtRequestSocketInvalid      %lu\t\t\t%lu\n",
                        atomic64_read( &g_mw_dbg_req_count.invalid ),
                        atomic64_read( &g_mw_dbg_resp_count.invalid ));
    
    mw_dbg_buff_append( "MtRequestSocketCreate       %lu\t\t\t%lu\n",
                        atomic64_read( &g_mw_dbg_req_count.create ),
                        atomic64_read( &g_mw_dbg_resp_count.create ) );

    mw_dbg_buff_append( "MtRequestSocketShutdown     %lu\t\t\t%lu\n",
                        atomic64_read( &g_mw_dbg_req_count.shutdown ),
                        atomic64_read( &g_mw_dbg_resp_count.shutdown ) );
    
    mw_dbg_buff_append( "MtRequestSocketClose        %lu\t\t\t%lu\n",
                        atomic64_read( &g_mw_dbg_req_count.close ),
                        atomic64_read( &g_mw_dbg_resp_count.close ) );
    
    mw_dbg_buff_append( "MtRequestSocketConnect      %lu\t\t\t%lu\n",
                        atomic64_read( &g_mw_dbg_req_count.connect ),
                        atomic64_read( &g_mw_dbg_resp_count.connect ));
    
    mw_dbg_buff_append( "MtRequestSocketBind         %lu\t\t\t%lu\n",
                        atomic64_read( &g_mw_dbg_req_count.bind ),
                        atomic64_read( &g_mw_dbg_resp_count.bind ) );
    
    mw_dbg_buff_append( "MtRequestSocketListen       %lu\t\t\t%lu\n",
                        atomic64_read( &g_mw_dbg_req_count.listen ),
                        atomic64_read( &g_mw_dbg_resp_count.listen ) );
    
    mw_dbg_buff_append( "MtRequestSocketAccept       %lu\t\t\t%lu\n",
                        atomic64_read( &g_mw_dbg_req_count.accept ),
                        atomic64_read( &g_mw_dbg_resp_count.accept ));
    
    mw_dbg_buff_append( "MtRequestSocketSend         %lu\t\t\t%lu\n",
                        atomic64_read( &g_mw_dbg_req_count.send ),
                        atomic64_read( &g_mw_dbg_resp_count.accept ) );
    
    mw_dbg_buff_append( "MtRequestSocketRecv         %lu\t\t\t%lu\n",
                        atomic64_read( &g_mw_dbg_req_count.recv ),
                        atomic64_read( &g_mw_dbg_resp_count.recv ) );

    mw_dbg_buff_append( "MtRequestSocketRecvFrom     %lu\t\t\t%lu\n",
                        atomic64_read( &g_mw_dbg_req_count.recvfrom ),
                        atomic64_read( &g_mw_dbg_resp_count.recvfrom ) );

    mw_dbg_buff_append( "MtRequestSocketGetName      %lu\t\t\t%lu\n",
                        atomic64_read( &g_mw_dbg_req_count.getname ),
                        atomic64_read( &g_mw_dbg_resp_count.getname ) );

    mw_dbg_buff_append( "MtRequestSocketGetPeer      %lu\t\t\t%lu\n",
                        atomic64_read( &g_mw_dbg_req_count.getpeer ),
                        atomic64_read( &g_mw_dbg_resp_count.getpeer ) );
    
    mw_dbg_buff_append( "MtRequestSocketAttrib       %lu\t\t\t%lu\n",
                        atomic64_read( &g_mw_dbg_req_count.attrib ),
                        atomic64_read( &g_mw_dbg_resp_count.attrib ) );
    
    mw_dbg_buff_append( "MtRequestSocketPollSetQuery %lu\t\t\t%lu\n",
                        atomic64_read( &g_mw_dbg_req_count.pollsetquery ),
                        atomic64_read( &g_mw_dbg_resp_count.pollsetquery ) );
    
    mw_dbg_buff_append( "Unknown                     %lu\t\t\t%lu\n",
                        atomic64_read( &g_mw_dbg_req_count.unknown ),
                        atomic64_read( &g_mw_dbg_resp_count.unknown ) );
    
    mw_dbg_buff_append( "---------------------------------------\n");
    
    mw_dbg_buff_append( "Total                       %lu\n",
                        atomic64_read( &g_mw_dbg_req_count.total ) );

    mw_dbg_buff_append( "\n\n" );
}


static ssize_t mw_debugfs_buffer_reader( struct file *fp, char __user *user_buffer,
                                        size_t count, loff_t *position )
{

    make_count_buffer();

    return simple_read_from_buffer( user_buffer,
                                    count,
                                    position,
                                    &mw_trace_buffer,
                                    MW_TRACE_BUFF_SIZE );
}

static ssize_t mw_debugfs_buffer_writer( struct file *fp, const char __user *user_buffer,
                                         size_t count, loff_t *position )
{

    return simple_write_to_buffer( &mw_trace_buffer,
                                   MW_TRACE_BUFF_SIZE,
                                   position,
                                   user_buffer,
                                   count );
}

static const struct file_operations fops_debug = {
    .read = mw_debugfs_buffer_reader,
    .write = mw_debugfs_buffer_writer,
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

    mw_tmp_dir = debugfs_create_file("message_counts", 0644, mw_debugfs_dir, &filevalue, &fops_debug);
    if( ! mw_tmp_dir )
    {
        printk("Could not create text file\n");
        goto ErrorExit;
    }

    mw_tmp_dir = debugfs_create_u64("tracing_on", 0644, mw_debugfs_dir, &mw_tracing_on );
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

    if( ! mw_tracing_on )
    {
        goto ErrorExit;
    }

    switch ( Request->base.type )
    {
    case MtRequestInvalid:
        atomic64_inc( &g_mw_dbg_req_count.invalid );
        atomic64_inc( &g_mw_dbg_req_count.total );
        break;
    case MtRequestSocketCreate:
        atomic64_inc( &g_mw_dbg_req_count.create );
        atomic64_inc( &g_mw_dbg_req_count.total );
        break;
    case MtRequestSocketShutdown:
        atomic64_inc( &g_mw_dbg_req_count.shutdown );
        atomic64_inc( &g_mw_dbg_req_count.total );
        break;
    case MtRequestSocketClose:
        atomic64_inc( &g_mw_dbg_req_count.close );
        atomic64_inc( &g_mw_dbg_req_count.total );
        break;
    case MtRequestSocketConnect:
        atomic64_inc( &g_mw_dbg_req_count.connect );
        atomic64_inc( &g_mw_dbg_req_count.total );
        break;
    case MtRequestSocketBind:
        atomic64_inc( &g_mw_dbg_req_count.bind );
        atomic64_inc( &g_mw_dbg_req_count.total );
        break;
    case MtRequestSocketListen:
        atomic64_inc( &g_mw_dbg_req_count.listen );
        atomic64_inc( &g_mw_dbg_req_count.total );
        break;
    case MtRequestSocketAccept:
        atomic64_inc( &g_mw_dbg_req_count.accept );
        atomic64_inc( &g_mw_dbg_req_count.total );
        break;
    case MtRequestSocketSend:
        atomic64_inc( &g_mw_dbg_req_count.send );
        atomic64_inc( &g_mw_dbg_req_count.total );
        break;
    case MtRequestSocketRecv:
        atomic64_inc( &g_mw_dbg_req_count.recv );
        atomic64_inc( &g_mw_dbg_req_count.total );
        break;
    case MtRequestSocketRecvFrom:
        atomic64_inc( &g_mw_dbg_req_count.recvfrom );
        atomic64_inc( &g_mw_dbg_req_count.total );
        break;
    case MtRequestSocketGetName:
        atomic64_inc( &g_mw_dbg_req_count.getname );
        atomic64_inc( &g_mw_dbg_req_count.total );
        break;
    case MtRequestSocketGetPeer:
        atomic64_inc( &g_mw_dbg_req_count.getpeer );
        atomic64_inc( &g_mw_dbg_req_count.total );
        break;
    case MtRequestSocketAttrib:
        atomic64_inc( &g_mw_dbg_req_count.attrib );
        atomic64_inc( &g_mw_dbg_req_count.total );
        break;
    case MtRequestPollsetQuery:
        atomic64_inc( &g_mw_dbg_req_count.pollsetquery );
        atomic64_inc( &g_mw_dbg_req_count.total );
        break;
    default:
        atomic64_inc( &g_mw_dbg_req_count.total );
        atomic64_inc( &g_mw_dbg_req_count.unknown );
    }

ErrorExit:
    
    return;
}

void mw_debugfs_response_count( mt_response_generic_t* Response )
{
        if( ! mw_tracing_on )
    {
        goto ErrorExit;
    }

    switch ( Request->base.type )
    {
    case MtRequestInvalid:
        atomic64_inc( &g_mw_dbg_req_count.invalid );
        atomic64_inc( &g_mw_dbg_req_count.total );
        break;
    case MtRequestSocketCreate:
        atomic64_inc( &g_mw_dbg_req_count.create );
        atomic64_inc( &g_mw_dbg_req_count.total );
        break;
    case MtRequestSocketShutdown:
        atomic64_inc( &g_mw_dbg_req_count.shutdown );
        atomic64_inc( &g_mw_dbg_req_count.total );
        break;
    case MtRequestSocketClose:
        atomic64_inc( &g_mw_dbg_req_count.close );
        atomic64_inc( &g_mw_dbg_req_count.total );
        break;
    case MtRequestSocketConnect:
        atomic64_inc( &g_mw_dbg_req_count.connect );
        atomic64_inc( &g_mw_dbg_req_count.total );
        break;
    case MtRequestSocketBind:
        atomic64_inc( &g_mw_dbg_req_count.bind );
        atomic64_inc( &g_mw_dbg_req_count.total );
        break;
    case MtRequestSocketListen:
        atomic64_inc( &g_mw_dbg_req_count.listen );
        atomic64_inc( &g_mw_dbg_req_count.total );
        break;
    case MtRequestSocketAccept:
        atomic64_inc( &g_mw_dbg_req_count.accept );
        atomic64_inc( &g_mw_dbg_req_count.total );
        break;
    case MtRequestSocketSend:
        atomic64_inc( &g_mw_dbg_req_count.send );
        atomic64_inc( &g_mw_dbg_req_count.total );
        break;
    case MtRequestSocketRecv:
        atomic64_inc( &g_mw_dbg_req_count.recv );
        atomic64_inc( &g_mw_dbg_req_count.total );
        break;
    case MtRequestSocketRecvFrom:
        atomic64_inc( &g_mw_dbg_req_count.recvfrom );
        atomic64_inc( &g_mw_dbg_req_count.total );
        break;
    case MtRequestSocketGetName:
        atomic64_inc( &g_mw_dbg_req_count.getname );
        atomic64_inc( &g_mw_dbg_req_count.total );
        break;
    case MtRequestSocketGetPeer:
        atomic64_inc( &g_mw_dbg_req_count.getpeer );
        atomic64_inc( &g_mw_dbg_req_count.total );
        break;
    case MtRequestSocketAttrib:
        atomic64_inc( &g_mw_dbg_req_count.attrib );
        atomic64_inc( &g_mw_dbg_req_count.total );
        break;
    case MtRequestPollsetQuery:
        atomic64_inc( &g_mw_dbg_req_count.pollsetquery );
        atomic64_inc( &g_mw_dbg_req_count.total );
        break;
    default:
        atomic64_inc( &g_mw_dbg_req_count.total );
        atomic64_inc( &g_mw_dbg_req_count.unknown );
    }

ErrorExit:
    
    return;

    return;
}

void mw_debugfs_fini()
{

    debugfs_remove_recursive( mw_debugfs_dir );

}
