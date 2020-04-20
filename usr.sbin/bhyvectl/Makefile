#
# $FreeBSD$
#

.include <src.opts.mk>

PROG=	bhyvectl
PACKAGE=	bhyve

LIBADD=	vmmapi util

WARNS?=	3

CFLAGS+= -I${SRCTOP}/sys/${MACHINE}/vmm

.if ${MK_BHYVE_SNAPSHOT} != "no"
CFLAGS+= -DBHYVE_SNAPSHOT
.endif

.include "${.CURDIR}/${MACHINE}/Makefile.inc"
.include <bsd.prog.mk>
