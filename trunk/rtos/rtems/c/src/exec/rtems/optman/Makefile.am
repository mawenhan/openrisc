##
##  $Id: Makefile.am,v 1.2 2001-09-27 11:59:19 chris Exp $
##

AUTOMAKE_OPTIONS = foreign 1.4

C_FILES = no-dpmem.c no-event.c no-msg.c no-mp.c no-part.c no-region.c \
    no-rtmon.c no-sem.c no-signal.c no-timer.c
C_O_FILES = $(C_FILES:%.c=${ARCH}/%.o)

PGMS = $(C_FILES:%.c=$(ARCH)/%.rel)

include $(RTEMS_ROOT)/make/custom/@RTEMS_BSP@.cfg
include $(top_srcdir)/../../../automake/lib.am

TMPINSTALL_FILES += \
    $(C_FILES:%.c=$(PROJECT_RELEASE)/lib/%$(LIB_VARIANT).rel)

$(PROJECT_RELEASE)/lib/%$(LIB_VARIANT).rel: $(ARCH)/%.rel
	$(INSTALL_DATA) $< $@

#
# (OPTIONAL) Add local stuff here using +=
#

ASM4FLAGS += -I$(PROJECT_INCLUDE)/rtems

all-local: ${ARCH} $(PGMS) $(TMPINSTALL_FILES)

.PRECIOUS: $(PGMS)

EXTRA_DIST = $(C_FILES)

include $(top_srcdir)/../../../automake/local.am
