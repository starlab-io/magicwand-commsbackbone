#ifndef _MEMINTERCEPT_H_
#define _MEMINTERCEPT_H_

void npf_mem_init( void );
void npf_mem_fini( void );

void * npf_emalloc( size_t );
void * npf_ecalloc( size_t, size_t );
void * npf_erealloc( void *, size_t );
void   npf_free( void * );

#endif //_MEMINTERCEPT_H_
