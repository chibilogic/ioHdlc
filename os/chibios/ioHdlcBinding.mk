# 
# ioHdlc ChibiOS binding files
# 
IOHDLCBINDDIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
IOHDLC_ROOT_DIR ?= $(abspath $(IOHDLCBINDDIR)/../..)

include $(IOHDLC_ROOT_DIR)/mk/ioHdlc_sources.mk

IOHDLCBINDINC := $(IOHDLCBINDDIR)/include
IOHDLCBINDSRC := $(IOHDLC_CHIBIOS_ALL_SRCS)

# Shared variables
ALLCSRC += $(IOHDLCBINDSRC) 
ALLINC  += $(IOHDLCBINDINC)

# Note: ioHdlcpool_common.c is in the core src/ directory, 
# not in os-specific directories. It should be included by 
# projects via IOHDLC_CORE_SRCS in their main Makefile.
