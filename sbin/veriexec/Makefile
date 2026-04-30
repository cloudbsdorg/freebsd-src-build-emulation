PROG=	veriexec
MAN=	veriexec.8
SRCS= \
	veriexec.c \
	manifest_parser.y \
	manifest_lexer.l

# Include paths for local libraries
CFLAGS+= -I${SRCTOP}/lib/libsecureboot/h
CFLAGS+= -I${SRCTOP}/lib/libveriexec
CFLAGS+= -I${SRCTOP}/contrib/bearssl/inc

# Link against local libraries using full paths
LDADD+= -L${OBJTOP}/lib/libveriexec -lveriexec
LDADD+= -L${OBJTOP}/lib/libsecureboot -lsecureboot
LDADD+= -L${OBJTOP}/contrib/bearssl/build -lbearssl

NO_SHARED=

.include <bsd.prog.mk>

CFLAGS+= -I${.CURDIR} ${XCFLAGS.${.TARGET:T:R}:U}

XCFLAGS.manifest_lexer+= -Wno-missing-variable-declarations \
	-Wno-unneeded-internal-declaration
XCFLAGS.manifest_parser+= -Wno-missing-variable-declarations
