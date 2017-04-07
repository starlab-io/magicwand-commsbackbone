/*-
 * Copyright (c) 2015 Antti Kantee.  All Rights Reserved.
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

#include <bmk-core/mainthread.h>
#include <bmk-core/printf.h>

#include <rumprun-base/config.h>
#include <rumprun-base/rumprun.h>

/*
 * for baking multiple executables into a single binary
 * TODO: remove hardcoded limit
 */
mainlike_fn rumprun_notmain;
mainlike_fn rumprun_main1;
mainlike_fn rumprun_main2;
mainlike_fn rumprun_main3;
mainlike_fn rumprun_main4;
mainlike_fn rumprun_main5;
mainlike_fn rumprun_main6;
mainlike_fn rumprun_main7;
mainlike_fn rumprun_main8;

#define RUNMAIN(i)							\
	if (rumprun_main##i == rumprun_notmain)				\
		break;							\
	rumprun(rre->rre_flags, rumprun_main##i,			\
	    rre->rre_argc, rre->rre_argv);				\
	if ((rre->rre_flags & RUMPRUN_EXEC_CMDLINE) == 0)		\
		rre = TAILQ_NEXT(rre, rre_entries);			\
	if (rre == NULL) {						\
		bmk_printf("out of argv entries\n");			\
		break;							\
	}

void
bmk_mainthread(void *cmdline)
{
	struct rumprun_exec *rre;
	void *cookie;

	rumprun_boot(cmdline);

	rre = TAILQ_FIRST(&rumprun_execs);
	do {
		RUNMAIN(1);
		RUNMAIN(2);
		RUNMAIN(3);
		RUNMAIN(4);
		RUNMAIN(5);
		RUNMAIN(6);
		RUNMAIN(7);
		RUNMAIN(8);
	} while (/*CONSTCOND*/0);

	while ((cookie = rumprun_get_finished()))
		rumprun_wait(cookie);

	rumprun_reboot();
}
