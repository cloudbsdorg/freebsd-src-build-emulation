# $FreeBSD$

PROG=	emu

# Enable bhyve/VMM support (set BHYVE_SUPPORT=no to disable)
# Default is yes to maintain backward compatibility
BHYVE_SUPPORT?=	yes

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
.if ${BHYVE_SUPPORT} == "yes"
SRCS+=	emu_bhyve.c
.endif
SRCS+=	emu_crash.c
SRCS+=	emu_share.c
SRCS+=	emu_image.c
# emu_mmio.c excluded - requires kernel APIs
SRCS+=	emu_dev_uart.c
SRCS+=	emu_dev_storage.c
SRCS+=	emu_dev_net.c
SRCS+=	emu_dev_rng.c
SRCS+=	emu_virtqueue.c
SRCS+=	emu_veriexec.c
SRCS+=	emu_module.c
SRCS+=	emu_sym.c
MAN=	emu.8 emu.conf.5

WARNS?=	3

CFLAGS+= -I${.CURDIR}
CFLAGS+= -I${SRCTOP}/lib/libsecureboot/h
CFLAGS+= -I${SRCTOP}/lib/libsecureboot/openpgp
CFLAGS+= -I${SRCTOP}/contrib/bearssl/inc
CFLAGS+= -DUSE_BEARSSL
CFLAGS+= -DCROSS_COMPILING

# Enable bhyve/VMM support (controlled by BHYVE_SUPPORT variable above)
.if ${BHYVE_SUPPORT} == "yes"
CFLAGS+= -DEMU_BHYVE_SUPPORT
.endif
LIBADD+= md
# libveriexec is built in lib/libveriexec
# Add the path to find it during build
CWARNFLAGS+= -I${SRCTOP}/lib/libveriexec
LDADD+= -L${OBJTOP}/lib/libveriexec -lveriexec
DPADD+= ${LIBCRYPTO}

.include <bsd.prog.mk>
