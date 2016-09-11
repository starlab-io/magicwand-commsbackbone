# This is run from the build directory, like:
#  <RUMPRUNROOT>/obj-amd64-xen-mwdriver/buildrump.sh/sys/rump/dev/lib/libxenevent$
# Therefore we actually have to go up the directory tree this far.
CPPFLAGS += -I../../../../../../../platform/xen/xen/include
CPPFLAGS += -I../../../../../../../include

# Search host xen includes, such that <xen/xen.h> can be included
CPPFLAGS += -Ixen

# Definitely do NOT define this. It breaks the build.
#CPPFLAGS+= -D__RUMP_KERNEL__

.PATH:	${.CURDIR}/../../../../dev

LIB=	rumpdev_xenevent
COMMENT=Magicwand support for communication with Xen via /dev/xenevent

IOCONF= XENEVENT.ioconf

SRCS=	xenevent.c xen_comms.c xenevent_device.c

# Do not rename symbols with these prefixes
# Compare to list in platform/xen/librumpxen_xendev/Makefile
RUMP_SYM_NORENAME=xenbus_|HYPERVISOR_|minios_|bmk_|gntmap|gnttab|_minios|_text

.include <bsd.lib.mk>
.include <bsd.klinks.mk>
