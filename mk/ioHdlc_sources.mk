# Common ioHdlc source lists.
#
# Include this file after defining ROOT_DIR, or define IOHDLC_ROOT_DIR
# explicitly before inclusion.

ifndef IOHDLC_ROOT_DIR
ifdef ROOT_DIR
IOHDLC_ROOT_DIR := $(ROOT_DIR)
else
IOHDLC_ROOT_DIR := .
endif
endif

IOHDLC_SRC_DIR ?= $(IOHDLC_ROOT_DIR)/src
IOHDLC_OS_LINUX_DIR ?= $(IOHDLC_ROOT_DIR)/os/linux
IOHDLC_OS_CHIBIOS_DIR ?= $(IOHDLC_ROOT_DIR)/os/chibios

IOHDLC_CORE_SRCS = \
	$(IOHDLC_SRC_DIR)/ioHdlc_core.c \
	$(IOHDLC_SRC_DIR)/ioHdlc.c \
	$(IOHDLC_SRC_DIR)/ioHdlc_log.c \
	$(IOHDLC_SRC_DIR)/ioHdlcll.c \
	$(IOHDLC_SRC_DIR)/ioHdlcswdriver.c \
	$(IOHDLC_SRC_DIR)/ioHdlcpool_common.c \
	$(IOHDLC_SRC_DIR)/ioHdlc_runner.c

IOHDLC_LINUX_BASE_SRCS = \
	$(IOHDLC_OS_LINUX_DIR)/src/ioHdlcosal.c \
	$(IOHDLC_OS_LINUX_DIR)/src/ioHdlcfmempool.c

IOHDLC_CHIBIOS_BASE_SRCS = \
	$(IOHDLC_OS_CHIBIOS_DIR)/src/ioHdlcosal.c \
	$(IOHDLC_OS_CHIBIOS_DIR)/src/ioHdlcdma.c \
	$(IOHDLC_OS_CHIBIOS_DIR)/src/ioHdlcfmempool.c

IOHDLC_CHIBIOS_UART_SRCS = \
	$(IOHDLC_OS_CHIBIOS_DIR)/src/ioHdlcstream_uart.c

IOHDLC_CHIBIOS_SPI_SRCS = \
	$(IOHDLC_OS_CHIBIOS_DIR)/src/ioHdlcstream_spi.c

IOHDLC_CHIBIOS_STREAM_SRCS = \
	$(IOHDLC_CHIBIOS_UART_SRCS) \
	$(IOHDLC_CHIBIOS_SPI_SRCS)

IOHDLC_CHIBIOS_ALL_SRCS = \
	$(IOHDLC_CHIBIOS_BASE_SRCS) \
	$(IOHDLC_CHIBIOS_STREAM_SRCS)
