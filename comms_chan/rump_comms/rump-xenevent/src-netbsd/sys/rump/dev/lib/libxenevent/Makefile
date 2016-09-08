#INCSDIR= /usr/include/xen
#INCSDIR= ../../../../platform/xen/xen/include

#INCSDIR= ../../../../../../platform/xen/xen/include
#INCSDIR  = "${TOPRUMP}/../../platform/xen/xen/include"
#INCSDIR += "${.CURDIR}/platform/xen/xen/include"
#INCSDIR += "${.CURDIR}/include"

# This is run from the build directory, like:
#  <RUMPRUNROOT>/obj-amd64-xen-mwdriver/buildrump.sh/sys/rump/dev/lib/libxenevent$
# Therefore we actually have to go up the directory tree this far.
CPPFLAGS += -I../../../../../../../platform/xen/xen/include
CPPFLAGS += -I../../../../../../../include

# Only install includes which are used by userland
#INCS=	mbppio.h

#	$NetBSD: Makefile,v 1.5 2016/01/26 23:12:14 pooka Exp $
#

.PATH:	${.CURDIR}/../../../../dev

LIB=	rumpdev_xenevent
COMMENT=Magicwand support for communication with Xen via /dev/xenevent

IOCONF= XENEVENT.ioconf

SRCS=	xenevent.c xen_comms.c xenevent_device.c

.include <bsd.lib.mk>
#.include <bsd.kinc.mk>
.include <bsd.klinks.mk>
