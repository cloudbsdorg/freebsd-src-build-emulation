#
# Emulation Framework Core Module Makefile
#
# This builds the emu_core.ko kernel module, which provides the core
# infrastructure for the emulation framework.
#

.PATH: ${SRCTOP}/sys/emulation
KMOD=	emu_core
SRCS=	emu_main.c emu_sysctl.c emu_instance.c emu_access.c emu_stack.c emu_securelevel.c emu_scrub.c emu_rctl.c

# Include standard kernel module build rules
.include <bsd.kmod.mk>
