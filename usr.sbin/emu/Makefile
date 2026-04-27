# $FreeBSD$

PROG=	emu
SRCS=	emu.c
SRCS+=	emu_init.c emu_start.c emu_stop.c emu_status.c emu_list.c
SRCS+=	emu_destroy.c emu_console.c
SRCS+=	emu_output.c
SRCS+=	emu_config.c
SRCS+=	emu_load.c emu_stack.c emu_test.c
SRCS+=	emu_snapshot.c emu_blob.c
SRCS+=	emu_imgman.c emu_image.c
MAN=	emu.8

WARNS?=	6

CFLAGS+= -I${.CURDIR}

.include <bsd.prog.mk>
