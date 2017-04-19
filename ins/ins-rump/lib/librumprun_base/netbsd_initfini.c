/*-
 * Copyright (c) 2014, 2015 Antti Kantee.  All Rights Reserved.
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

#include <sys/exec_elf.h>
#include <sys/exec.h>

#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bmk-core/core.h>
#include <bmk-core/printf.h>

#include <rumprun-base/config.h>

#include "rumprun-private.h"

static char ssbuf[32];
static char *initial_env[] = {
	ssbuf,
	NULL,
};

extern void *environ;
extern char *__progname;
extern struct ps_strings *__ps_strings;

void _libc_init(void);

typedef void (*initfini_fn)(void);
extern const initfini_fn __init_array_start[1];
extern const initfini_fn __init_array_end[1];
extern const initfini_fn __fini_array_start[1];
extern const initfini_fn __fini_array_end[1];

void *__dso_handle;

static void
runinit(void)
{
	const initfini_fn *fn;

	_libc_init();
	for (fn = __init_array_start; fn < __init_array_end; fn++)
		(*fn)();
}

static void
runfini(void)
{
	const initfini_fn *fn;

	for (fn = __fini_array_start; fn < __fini_array_end; fn++)
		(*fn)();
}

struct initinfo {
	char *argv_dummy;
	char *env_dummy;
	AuxInfo ai[2];
} __attribute__((__packed__));

void
_netbsd_userlevel_init(void)
{
	static struct initinfo ii;
	static struct ps_strings ps;
	AuxInfo *ai = ii.ai;
	int rv;

	ai[0].a_type = AT_STACKBASE;
	ai[0].a_v = (unsigned long)bmk_mainstackbase;
	ai[1].a_type = AT_NULL;
	ai[1].a_v = 0;

	ps.ps_argvstr = &ii.argv_dummy;
	__ps_strings = &ps;

	/*
	 * We get no "environ" from the kernel.  The initial
	 * environment is created by rumprun_boot() depending on
	 * what environ arguments were given (if any).
	 */
	rv = bmk_snprintf(ssbuf, sizeof(ssbuf),
	    "PTHREAD_STACKSIZE=%zu", RUMPRUN_DEFAULTUSERSTACK);
	bmk_assert(rv < (int)sizeof(ssbuf));
	environ = initial_env;

	runinit();

	/* XXX: we should probably use csu, but this is quicker for now */
	__progname = strdup("rumprun");
}

void
_netbsd_userlevel_fini(void)
{

	runfini();
}
