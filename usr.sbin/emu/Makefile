# $FreeBSD$

PROG=	emu
SRCS=	emu.c
SRCS+=	emu_init.c emu_start.c emu_stop.c emu_status.c emu_list.c
SRCS+=	emu_destroy.c emu_console.c
SRCS+=	emu_output.c
SRCS+=	emu_config.c
SRCS+=	emu_load.c emu_unload.c emu_stack.c emu_test.c
SRCS+=	emu_snapshot.c emu_blob.c
SRCS+=	emu_engine.c
SRCS+=	emu_boot.c
SRCS+=	emu_decoder.c
SRCS+=	emu_bhyve.c
SRCS+=	emu_crash.c
SRCS+=	emu_share.c
MAN=	emu.8

WARNS?=	6

CFLAGS+= -I${.CURDIR}

.include <bsd.prog.mk>
