# $FreeBSD$

PROG=	emu
SRCS=	emu.c
SRCS+=	emu_init.c emu_start.c emu_stop.c emu_status.c emu_list.c
SRCS+=	emu_destroy.c emu_console.c
SRCS+=	emu_output.c
SRCS+=	emu_config.c
SRCS+=	emu_load.c emu_stack.c emu_test.c
SRCS+=	emu_snapshot.c emu_blob.c
SRCS+=	emu_engine.c
SRCS+=	emu_file_sharing.c
SRCS+=	emu_zfs.c
SRCS+=	emu_boot.c
SRCS+=	emu_decoder.c
SRCS+=	emu_bhyve.c
SRCS+=	emu_crash.c
SRCS+=	emu_share.c
SRCS+=	emu_image.c
SRCS+=	emu_mmio.c
SRCS+=	emu_dev_uart.c
SRCS+=	emu_dev_storage.c
SRCS+=	emu_dev_net.c
SRCS+=	emu_gdb.c
MAN=	emu.8

WARNS?=	6

CFLAGS+= -I${.CURDIR}

.include <bsd.prog.mk>
