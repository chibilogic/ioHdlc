# 
# ioHdlc ChibiOS binding files
# 
IOHDLCBINDDIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

IOHDLCBINDINC := $(IOHDLCBINDDIR)/include
IOHDLCBINDSRC := $(IOHDLCBINDDIR)/src/ioHdlcosal.c     \
                 $(IOHDLCBINDDIR)/src/ioHdlcfmempool.c \
				 $(IOHDLCBINDDIR)/src/ioHdlcstream_uart.c \
				 $(IOHDLCBINDDIR)/src/ioHdlc_runner.c

# Shared variables
ALLCSRC += $(IOHDLCBINDSRC) 
ALLINC  += $(IOHDLCBINDINC)
