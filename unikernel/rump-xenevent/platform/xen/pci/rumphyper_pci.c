/*-
 * Copyright (c) 2013 Antti Kantee.  All Rights Reserved.
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

#include <mini-os/os.h>
#include <mini-os/ioremap.h>
#include <mini-os/pcifront.h>
#include <mini-os/events.h>
#include <mini-os/mm.h>
#include <mini-os/hypervisor.h>

#include <bmk-core/errno.h>
#include <bmk-core/memalloc.h>

#include "pci_user.h"

void *
rumpcomp_pci_map(unsigned long addr, unsigned long len)
{

	return minios_ioremap_nocache(addr, len);
}

int
rumpcomp_pci_confread(unsigned bus, unsigned dev, unsigned fun,
	int reg, unsigned int *rv)
{

	return pcifront_conf_read(NULL, 0, bus, dev, fun, reg, 4, rv);
}

int
rumpcomp_pci_confwrite(unsigned bus, unsigned dev, unsigned fun,
	int reg, unsigned int v)
{

	return pcifront_conf_write(NULL, 0, bus, dev, fun, reg, 4, v);
}

struct ihandler {
	int (*i_handler)(void *);
	void *i_data;
	evtchn_port_t i_prt;
};

static void
hyperhandler(evtchn_port_t prt, struct pt_regs *regs, void *data)
{
	struct ihandler *ihan = data;

	/* XXX: not correct, might not even have rump kernel context now */
	ihan->i_handler(ihan->i_data);
}

/* XXXXX */
static int myintr;
static unsigned mycookie;

int
rumpcomp_pci_irq_map(unsigned bus, unsigned device, unsigned fun,
	int intrline, unsigned cookie)
{

	/* XXX */
	myintr = intrline;
	mycookie = cookie;

	return 0;
}

void *
rumpcomp_pci_irq_establish(unsigned cookie, int (*handler)(void *), void *data)
{
	struct ihandler *ihan;
	evtchn_port_t prt;
	int pirq;

	if (cookie != mycookie)
		return NULL;
	pirq = myintr;

	ihan = bmk_memalloc(sizeof(*ihan), 0, BMK_MEMWHO_WIREDBMK);
	if (!ihan)
		return NULL;
	ihan->i_handler = handler;
	ihan->i_data = data;

	prt = minios_bind_pirq(pirq, 1, hyperhandler, ihan);
	minios_unmask_evtchn(prt);
	ihan->i_prt = prt;

	return ihan;
}
