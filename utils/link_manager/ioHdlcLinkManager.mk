# Optional ioHdlc link manager utility.

IOHDLC_LINK_MANAGER_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

IOHDLC_LINK_MANAGER_INC := $(IOHDLC_LINK_MANAGER_DIR)/include
IOHDLC_LINK_MANAGER_SRCS := \
	$(IOHDLC_LINK_MANAGER_DIR)/src/ioHdlc_link_manager.c
