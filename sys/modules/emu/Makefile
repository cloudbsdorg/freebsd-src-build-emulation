#
# Emulation Framework Master Module Makefile
#
# This is the master module that pulls in all emulation sub-modules.
# It contains no source code.
# Loading this module via kldload emu will load all emulation modules.
#
# Note: Module dependencies are declared in sys/modules/Makefile via
# the _emulation variable, which ensures proper build and load order.
#

KMOD=	emu
SRCS=
MAN=	emulation.4

# Include standard kernel module build rules
.include <bsd.kmod.mk>
