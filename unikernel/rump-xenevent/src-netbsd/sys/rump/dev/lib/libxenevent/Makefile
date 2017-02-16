# This is run from the build directory, like:
#  <RUMPRUNROOT>/obj-amd64-xen/buildrump.sh/sys/rump/dev/lib/libxenevent$
# Therefore we actually have to go up the directory tree this far.
CPPFLAGS += -I../../../../../../../platform/xen/xen/include
CPPFLAGS += -I../../../../../../../include
CPPFLAGS += -I../../../../../../../../../common

# Search host xen includes, such that <xen/xen.h> can be included
CPPFLAGS += -Ixen

# Enable debugging - breakpoints and messages
#CPPFLAGS += -DMYDEBUG
#CPPFLAGS += -DMYTRAP

# Definitely do NOT define this. It breaks the build.
#CPPFLAGS+= -D__RUMP_KERNEL__

.PATH:	${.CURDIR}/../../../../dev

LIB = rumpdev_xenevent
COMMENT = Magicwand support for communication with xenevents device via /dev/xe

IOCONF = XENEVENT.ioconf

SRCS = xenevent.c \
       xenevent_comms.c \
       xenevent_device.c \
       xenevent_netbsd.c \
       xenevent_minios.c

# Tell the Rump build system not to rename symbols with these prefixes
# Compare to list in platform/xen/librumpxen_xendev/Makefile
RUMP_SYM_NORENAME = xenbus_|HYPERVISOR_|minios_|bmk_|gntmap|gnttab|_minios|_text

.include <bsd.lib.mk>
.include <bsd.klinks.mk>
