/* -*-  Mode:C; c-basic-offset:4; tab-width:4 -*-
 ****************************************************************************
 * (C) 2003 - Rolf Neugebauer - Intel Research Cambridge
 * (C) 2005 - Grzegorz Milos - Intel Research Cambridge
 ****************************************************************************
 *
 *        File: events.c
 *      Author: Rolf Neugebauer (neugebar@dcs.gla.ac.uk)
 *     Changes: Grzegorz Milos (gm281@cam.ac.uk)
 *
 *        Date: Jul 2003, changes Jun 2005
 *
 * Environment: Xen Minimal OS
 * Description: Deals with events recieved on event channels
 *
 ****************************************************************************
 */

#include <mini-os/os.h>
#include <mini-os/mm.h>
#include <mini-os/hypervisor.h>
#include <mini-os/events.h>
#include <mini-os/lib.h>
#include <mini-os/wait.h>

#define NR_EVS 1024

/* this represents a event handler. Chaining or sharing is not allowed */
typedef struct _ev_action_t {
    spinlock_t lock;
    evtchn_handler_t handler;
    void *data;
    uint32_t count;
} ev_action_t;

static ev_action_t ev_actions[NR_EVS];
void default_handler(evtchn_port_t port, struct pt_regs *regs, void *data);

static unsigned long bound_ports[NR_EVS/(8*sizeof(unsigned long))];

static void (*rump_evtdev_callback)(u_int port);

void minios_events_register_rump_callback(void (*cb)(u_int))
{
    BUG_ON(rump_evtdev_callback != NULL);
    rump_evtdev_callback = cb;
}

/* interrupt handler queues events here */
DECLARE_WAIT_QUEUE_HEAD(minios_events_waitq);
static DEFINE_SPINLOCK(evt_handler_lock);
void minios_evtdev_handler(evtchn_port_t port, struct pt_regs * regs,
                           void *data)
{
    unsigned long flags;

    minios_mask_evtchn(port);

    local_irq_save(flags);
    spin_lock(&evt_handler_lock);

    rump_evtdev_callback(port);

    spin_unlock(&evt_handler_lock);
    local_irq_restore(flags);
}

void unbind_all_ports(void)
{
    int i;
    int cpu = 0;
    shared_info_t *s = HYPERVISOR_shared_info;
    vcpu_info_t   *vcpu_info = &s->vcpu_info[cpu];
    int rc;

    for (i = 0; i < NR_EVS; i++)
    {
        if (i == start_info.console.domU.evtchn ||
            i == start_info.store_evtchn)
            continue;

        if (test_and_clear_bit(i, bound_ports))
        {
            struct evtchn_close close;
            minios_printk("port %d still bound!\n", i);
            minios_mask_evtchn(i);

            spin_lock(&ev_actions[i].lock);
            ev_actions[i].handler = default_handler;
            wmb();
            ev_actions[i].data = NULL;
            spin_unlock(&ev_actions[i].lock);

            close.port = i;
            rc = HYPERVISOR_event_channel_op(EVTCHNOP_close, &close);
            if (rc)
                minios_printk("WARN: close_port %s failed rc=%d. ignored\n", i, rc);
            minios_clear_evtchn(i);
        }
    }
    vcpu_info->evtchn_upcall_pending = 0;
    vcpu_info->evtchn_pending_sel = 0;
}

/*
 * Demux events to different handlers.
 */
int do_event(evtchn_port_t port, struct pt_regs *regs)
{
    ev_action_t  *action;

    minios_clear_evtchn(port);

    if (port >= NR_EVS)
    {
        minios_printk("WARN: do_event(): Port number too large: %d\n", port);
        return 1;
    }

    spin_lock(&ev_actions[port].lock);
    action = &ev_actions[port];
    action->count++;

    /* call the handler */
    action->handler(port, regs, action->data);
    spin_unlock(&ev_actions[port].lock);

    return 1;
}

evtchn_port_t minios_bind_evtchn(evtchn_port_t port, evtchn_handler_t handler,
                                 void *data)
{
    spin_lock(&ev_actions[port].lock);
    if (ev_actions[port].handler != default_handler)
        minios_printk("WARN: Handler for port %d already registered, replacing\n",
                      port);

    ev_actions[port].data = data;
    wmb();
    ev_actions[port].handler = handler;
    spin_unlock(&ev_actions[port].lock);

    set_bit(port, bound_ports);

    return port;
}

void minios_unbind_evtchn(evtchn_port_t port)
{
    struct evtchn_close close;
    int rc;

    spin_lock(&ev_actions[port].lock);
    if (ev_actions[port].handler == default_handler)
        minios_printk("WARN: No handler for port %d when unbinding\n", port);
    minios_mask_evtchn(port);
    minios_clear_evtchn(port);

    ev_actions[port].handler = default_handler;
    wmb();
    ev_actions[port].data = NULL;
    spin_unlock(&ev_actions[port].lock);

    clear_bit(port, bound_ports);

    close.port = port;
    rc = HYPERVISOR_event_channel_op(EVTCHNOP_close, &close);
    if (rc)
        minios_printk("WARN: close_port %s failed rc=%d. ignored\n", port, rc);
}

evtchn_port_t minios_bind_virq(uint32_t virq, evtchn_handler_t handler,
                               void *data)
{
    evtchn_bind_virq_t op;
    int rc;

    /* Try to bind the virq to a port */
    op.virq = virq;
    op.vcpu = smp_processor_id();

    if ((rc = HYPERVISOR_event_channel_op(EVTCHNOP_bind_virq, &op)) != 0)
    {
        minios_printk("Failed to bind virtual IRQ %d with rc=%d\n", virq, rc);
        return -1;
    }
    minios_bind_evtchn(op.port, handler, data);
    return op.port;
}

evtchn_port_t minios_bind_pirq(uint32_t pirq, int will_share,
                               evtchn_handler_t handler, void *data)
{
    evtchn_bind_pirq_t op;
    int rc;

    /* Try to bind the pirq to a port */
    op.pirq = pirq;
    op.flags = will_share ? BIND_PIRQ__WILL_SHARE : 0;

    if ((rc = HYPERVISOR_event_channel_op(EVTCHNOP_bind_pirq, &op)) != 0)
    {
        minios_printk("Failed to bind physical IRQ %d with rc=%d\n", pirq, rc);
        return -1;
    }
    minios_bind_evtchn(op.port, handler, data);
    return op.port;
}

#if defined(__x86_64__)
static char irqstack[2 * STACK_SIZE];

static struct pda
{
    int irqcount;       /* offset 0 (used in x86_64.S) */
    char *irqstackptr;  /*        8 */
} cpu0_pda;
#endif

/* Just a simple wrapper for event channel hypercall. */
int minios_event_channel_op(int cmd, void *op)
{
    return HYPERVISOR_event_channel_op(cmd, op);
}

/*
 * Initially all events are without a handler and disabled
 */
void init_events(void)
{
    int i;
#if defined(__x86_64__)
    asm volatile("movl %0,%%fs ; movl %0,%%gs" :: "r" (0));
    wrmsrl(0xc0000101, &cpu0_pda); /* 0xc0000101 is MSR_GS_BASE */
    cpu0_pda.irqcount = -1;
    cpu0_pda.irqstackptr = (void*) (((unsigned long)irqstack + 2 * STACK_SIZE)
                                    & ~(STACK_SIZE - 1));
#endif
    /* initialize event handler */
    for ( i = 0; i < NR_EVS; i++ )
    {
        ev_actions[i].handler = default_handler;
        spin_lock_init(&ev_actions[i].lock);
        minios_mask_evtchn(i);
    }
}

void fini_events(void)
{
    /* Dealloc all events */
    unbind_all_ports();
#if defined(__x86_64__)
    wrmsrl(0xc0000101, NULL); /* 0xc0000101 is MSR_GS_BASE */
#endif
}

void default_handler(evtchn_port_t port, struct pt_regs *regs, void *ignore)
{
    minios_printk("[Port %d] - event received\n", port);
}

/* Create a port available to the pal for exchanging notifications.
   Returns the result of the hypervisor call. */

/* Unfortunate confusion of terminology: the port is unbound as far
   as Xen is concerned, but we automatically bind a handler to it
   from inside mini-os. */

int minios_evtchn_alloc_unbound(domid_t pal, evtchn_handler_t handler,
                                void *data, evtchn_port_t *port)
{
    int rc;

    evtchn_alloc_unbound_t op;
    op.dom = DOMID_SELF;
    op.remote_dom = pal;
    rc = HYPERVISOR_event_channel_op(EVTCHNOP_alloc_unbound, &op);
    if (rc)
    {
        minios_printk("ERROR: alloc_unbound failed with rc=%d", rc);
        return rc;
    }
    *port = minios_bind_evtchn(op.port, handler, data);
    return rc;
}

/* Connect to a port so as to allow the exchange of notifications with
   the pal. Returns the result of the hypervisor call. */

int minios_evtchn_bind_interdomain(domid_t pal, evtchn_port_t remote_port,
                                   evtchn_handler_t handler, void *data,
                                   evtchn_port_t *local_port)
{
    int rc;
    evtchn_port_t port;
    evtchn_bind_interdomain_t op;
    op.remote_dom = pal;
    op.remote_port = remote_port;
    rc = HYPERVISOR_event_channel_op(EVTCHNOP_bind_interdomain, &op);
    if (rc)
    {
        minios_printk("ERROR: bind_interdomain failed with rc=%d", rc);
        return rc;
    }
    port = op.local_port;
    *local_port = minios_bind_evtchn(port, handler, data);
    return rc;
}

int minios_notify_remote_via_evtchn(evtchn_port_t port)
{
    evtchn_send_t op;
    op.port = port;
    return HYPERVISOR_event_channel_op(EVTCHNOP_send, &op);
}

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
