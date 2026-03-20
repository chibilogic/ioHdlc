# 
# ioHdlc ChibiOS binding files
# 
IOHDLCBINDDIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

IOHDLCBINDINC := $(IOHDLCBINDDIR)/include
IOHDLCBINDSRC := $(IOHDLCBINDDIR)/src/ioHdlcosal.c     \
                 $(IOHDLCBINDDIR)/src/ioHdlcfmempool.c \
				 $(IOHDLCBINDDIR)/src/ioHdlcstream_uart.c \
				 $(IOHDLCBINDDIR)/src/ioHdlcstream_spi.c

# Shared variables
ALLCSRC += $(IOHDLCBINDSRC) 
ALLINC  += $(IOHDLCBINDINC)

# Note: ioHdlcpool_common.c is in the core src/ directory, 
# not in os-specific directories. It should be included by 
# projects via IOHDLC_CORE_SRCS in their main Makefile.
