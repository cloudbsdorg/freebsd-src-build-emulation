#
# Emulation Framework Master Module Makefile
#
# This is the master module that pulls in all emulation sub-modules.
# It contains no source code, only MODULE_DEPEND declarations.
# Loading this module via kldload emu will load all emulation modules.
#

KMOD=	emu
SRCS=
MAN=	emulation.4

# Module dependencies - these ensure proper load order
# The master module depends on all sub-modules
MODULE_DEPEND(emu, emu_core, 1, 1, 1)
MODULE_DEPEND(emu, emu_amd64, 1, 1, 1)
MODULE_DEPEND(emu, emu_i386, 1, 1, 1)
MODULE_DEPEND(emu, emu_arm64, 1, 1, 1)
MODULE_DEPEND(emu, emu_arm, 1, 1, 1)
MODULE_DEPEND(emu, emu_powerpc, 1, 1, 1)
MODULE_DEPEND(emu, emu_riscv, 1, 1, 1)

# Include standard kernel module build rules
.include <bsd.kmod.mk>
