# $FreeBSD$

PROG=	emu
SRCS=	emu.c
SRCS+=	emu_init.c emu_start.c emu_stop.c emu_status.c emu_list.c
SRCS+=	emu_destroy.c emu_console.c
SRCS+=	emu_output.c
MAN=	emu.8

WARNS?=	6

CFLAGS+= -I${.CURDIR}

.include <bsd.prog.mk>
