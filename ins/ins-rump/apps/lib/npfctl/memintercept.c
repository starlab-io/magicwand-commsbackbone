/*************************************************************************
* STAR LAB PROPRIETARY & CONFIDENTIAL
* Copyright (C) 2019, Star Lab — All Rights Reserved
* Unauthorized copying of this file, via any medium is strictly prohibited.
***************************************************************************/

/**
 * 
 * 
 * @file memintercept.c
 * @author Alex Speasmaker
 * @date 1 January 2019
 * @brief Magicwand garbage collector to chagne npfctl CLI into a libarary call
 * 
 * This is a simple nieve garbage collector to make sure that free and malloc
 * calls are handled correctly in the npfctl code.
 * All calls to emalloc and ecallc, and erealloc have been wreplaced with calls to these
 * functions.
 * 
 * The way it works is keeping a list of all pointers malloce'd by the code between calls
 * npf_mem_init and npf_mem_fini and at the time npf_mem_fini is called, all memory is cleared
 *  
**/
 
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <util.h>

#include "memintercept.h"

int arr_elem = 256;
int malloc_count = 0;
int free_count = 0;

static void ** ptr_array;
static void ** temp_ptr_array;

void
npf_mem_init( void )
{
    ptr_array = ecalloc( arr_elem, sizeof( void * ) );
}


static void
realloc_swap( void * old_ptr, void * new_ptr )
{
    for( int i = 0; i < malloc_count; i++ )
    {
        if( ptr_array[i] == NULL )
            continue;

        if( ptr_array[i] == old_ptr )
        {
            ptr_array[i] = new_ptr;
            return;
        }
    }
}

static int
check_malloc( void )
{
    size_t new_arr_elem = 0;
    int err = 0;
    
    if( malloc_count > arr_elem )
    {
        new_arr_elem = ( arr_elem * 2 );
        temp_ptr_array = erealloc( ptr_array, ( new_arr_elem * sizeof( void * ) ) );
        if( NULL == ptr_array )
        {
            err = errno;
            perror( "Could not realloc memory for garbage collector" );
            errno = err;
            return -1;
        }
        
        arr_elem = new_arr_elem;
        ptr_array = temp_ptr_array;
        temp_ptr_array = NULL;
    }
    
    return 0;
}

void
npf_mem_fini( void )
{
    for( int i = 0; i <= malloc_count; i++ )
    {
        if( NULL == ptr_array[ i ] )
        {
            continue;
        }

        free( ptr_array[ i ] );
        ptr_array[ i ] = NULL;
        free_count++;
    }

    fflush( stdout );
    
    if( free_count != malloc_count )
    {
        perror( "The npfctl garbage collector has detected a memory leak somewhere\n" );
    }
}

void *
npf_emalloc( size_t size )
{
    void * ret = NULL;
    if( check_malloc() )
    {
        errno = ENOMEM;
        return NULL;
    }

    ret = emalloc( size );
    if( NULL != ret )
    {
        ptr_array[ malloc_count ] = ret;
        malloc_count++;
    }

    return ret;
}

void *
npf_ecalloc( size_t n, size_t size )
{
    void * ret = NULL;
    if( check_malloc() )
    {
        return NULL;
    }

    ret = ecalloc( n, size );
    if( NULL != ret )
    {
        ptr_array[ malloc_count ] = ret;
        malloc_count++;
    }
    
    return ret;
}

void *
npf_erealloc( void * cp, size_t nbytes )
{
    void * new_ptr = NULL;
    void * old_ptr = cp;
    
    new_ptr = erealloc( cp, nbytes );
    if( NULL != new_ptr )
    {
        realloc_swap( old_ptr, new_ptr );
    }
    
    return new_ptr;
}

void
npf_free( void * cp )
{
    //do nothing
}
