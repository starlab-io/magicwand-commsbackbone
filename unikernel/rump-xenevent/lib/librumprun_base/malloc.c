/*-
 * Copyright (c) 2014 Antti Kantee.  All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>

#include <stdlib.h>

#include <bmk-core/errno.h>
#include <bmk-core/memalloc.h>

int
posix_memalign(void **rv, size_t align, size_t nbytes)
{
	void *v;
	int error = BMK_ENOMEM;

	if ((v = bmk_memalloc(nbytes, align, BMK_MEMWHO_USER)) != NULL) {
		*rv = v;
		error = 0;
	}

	return error;
}

void *
malloc(size_t size)
{

	return bmk_memalloc(size, 8, BMK_MEMWHO_USER);
}

void *
calloc(size_t n, size_t size)
{

	return bmk_memcalloc(n, size, BMK_MEMWHO_USER);
}

void *
realloc(void *cp, size_t nbytes)
{

	return bmk_memrealloc_user(cp, nbytes);
}

void
free(void *cp)
{

	bmk_memfree(cp, BMK_MEMWHO_USER);
}
