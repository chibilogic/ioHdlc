##############################################################################
# ioHdlc Test Suite - ChibiOS/ARM frontend for MIP104
##############################################################################

.DEFAULT_GOAL := shell-spi1

TEST_CHIBIOS_FRONTEND_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
TESTS_CHIBIOS_DIR := $(abspath $(TEST_CHIBIOS_FRONTEND_DIR)/..)
ROOT_DIR := $(abspath $(TESTS_CHIBIOS_DIR)/../..)
SRC_DIR := $(ROOT_DIR)/src
INCLUDE_DIR := $(ROOT_DIR)/include
OS_CHIBIOS_DIR := $(ROOT_DIR)/os/chibios
COMMON_DIR := $(TESTS_CHIBIOS_DIR)/../common
COMMON_SCENARIOS_DIR := $(COMMON_DIR)/scenarios
CONFDIR := $(TEST_CHIBIOS_FRONTEND_DIR)/conf
FRONTEND_ADAPTERS_DIR := $(TEST_CHIBIOS_FRONTEND_DIR)/adapters
BOARD_CONFIG_DIR := $(TEST_CHIBIOS_FRONTEND_DIR)/board_config
IOHDLC_CHIBIOS_PORT_DIR := $(OS_CHIBIOS_DIR)/ports/SAMA/SAMA5D2x
CHIBIOS ?= /opt/gbc/sama5d2x/ChibiOS
SPI_MASTER ?= spi1
BUILDDIR ?= build
DEPDIR ?= .dep

TARGET_LABEL = MIP104

##############################################################################
# Build global options
##############################################################################

ifeq ($(USE_OPT),)
  USE_OPT = -O2 -ggdb -fomit-frame-pointer -falign-functions=16
endif

ifeq ($(USE_COPT),)
  USE_COPT =
endif

ifeq ($(USE_CPPOPT),)
  USE_CPPOPT = -fno-rtti
endif

ifeq ($(USE_LINK_GC),)
  USE_LINK_GC = yes
endif

ifeq ($(USE_LDOPT),)
  USE_LDOPT =
endif

ifeq ($(USE_LTO),)
  USE_LTO = no
endif

ifeq ($(USE_THUMB),)
  USE_THUMB = no
endif

ifeq ($(USE_VERBOSE_COMPILE),)
  USE_VERBOSE_COMPILE = no
endif

ifeq ($(USE_SMART_BUILD),)
  USE_SMART_BUILD = yes
endif

##############################################################################
# Architecture specific options
##############################################################################

ifeq ($(USE_SYSTEM_STACKSIZE),)
  USE_SYSTEM_STACKSIZE = 0x2000
endif

ifeq ($(USE_EXCEPTIONS_STACKSIZE),)
  USE_IRQ_STACKSIZE = 0x800
endif

ifeq ($(USE_FIQ_STACKSIZE),)
  USE_FIQ_STACKSIZE = 0x800
endif

ifeq ($(USE_SUPERVISOR_STACKSIZE),)
  USE_SUPERVISOR_STACKSIZE = 8
endif

ifeq ($(USE_UND_STACKSIZE),)
  USE_UND_STACKSIZE = 8
endif

ifeq ($(USE_ABT_STACKSIZE),)
  USE_ABT_STACKSIZE = 8
endif

ifeq ($(USE_FPU),)
  USE_FPU = no
endif

##############################################################################
# Project, sources and paths
##############################################################################

PROJECT = iohdlc_shell
TEST_MAIN_SRC = $(TEST_CHIBIOS_FRONTEND_DIR)/main_shell.c
TSERVICES_SRC = $(TEST_CHIBIOS_FRONTEND_DIR)/tservices.c
VERSION_SRC = $(TEST_CHIBIOS_FRONTEND_DIR)/version.c

include $(ROOT_DIR)/mk/ioHdlc_version.mk
include $(ROOT_DIR)/mk/ioHdlc_sources.mk

MCU = cortex-a5

include $(CHIBIOS)/os/license/license.mk
include $(CHIBIOS)/os/common/startup/ARMCAx-TZ/compilers/GCC/mk/startup_sama5d2.mk
include $(CHIBIOS)/os/hal/hal.mk
include $(CHIBIOS)/os/hal/ports/SAMA/SAMA5D2x/platform.mk
include $(CHIBIOS)/os/hal/boards/MIP104/board.mk
include $(CHIBIOS)/os/hal/osal/rt/osal.mk
include $(CHIBIOS)/os/rt/rt.mk
include $(CHIBIOS)/os/common/ports/ARMCAx-TZ/compilers/GCC/mk/port_generic_II.mk
include $(CHIBIOS)/os/tssi/ports/ARMCAx-TZ/SAMA5D2x/porttssi.mk
include $(CHIBIOS)/os/tssi/tssi.mk
include $(CHIBIOS)/os/various/shell/shell.mk
include $(CHIBIOS)/os/hal/lib/streams/streams.mk

LDSCRIPT = $(STARTUPLD)/BV9000CL.ld

##############################################################################
# Source files
##############################################################################

CHIBIOS_CONFIG_SRCS = $(TESTS_CHIBIOS_DIR)/test_config_shell.c

COMMON_TEST_SRCS = \
	$(COMMON_DIR)/test_framework.c \
	$(COMMON_DIR)/test_helpers.c \
	$(COMMON_DIR)/test_arenas.c

FRONTEND_SRCS = \
	$(FRONTEND_ADAPTERS_DIR)/adapter_spi.c \
	$(IOHDLC_CHIBIOS_PORT_DIR)/ioHdlcstream_spi_platform.c

COMMON_TEST_SCENARIOS = $(COMMON_SCENARIOS_DIR)/test_exchange.c

CSRC = $(ALLCSRC) \
       $(IOHDLC_CORE_SRCS) \
       $(IOHDLC_CHIBIOS_BASE_SRCS) \
       $(IOHDLC_CHIBIOS_SPI_SRCS) \
       $(CHIBIOS_CONFIG_SRCS) \
       $(COMMON_TEST_SRCS) \
       $(FRONTEND_SRCS) \
       $(COMMON_TEST_SCENARIOS) \
       $(TSERVICES_SRC) \
       $(VERSION_SRC) \
       $(TEST_MAIN_SRC)

CPPSRC = $(ALLCPPSRC)
ACSRC =
ACPPSRC =
TCSRC =
TCPPSRC =
ASMSRC = $(ALLASMSRC)
ASMXSRC = $(ALLXASMSRC)

INCDIR = $(CONFDIR) \
         $(INCLUDE_DIR) \
         $(OS_CHIBIOS_DIR)/include \
         $(COMMON_DIR) \
         $(COMMON_DIR)/adapters \
         $(TESTS_CHIBIOS_DIR) \
         $(IOHDLC_CHIBIOS_PORT_DIR) \
         $(FRONTEND_ADAPTERS_DIR) \
         $(BOARD_CONFIG_DIR) \
         $(ALLINC)

##############################################################################
# Compiler settings
##############################################################################

TRGT ?= arm-none-eabi-
CC   = $(TRGT)gcc
CPPC = $(TRGT)g++
LD   = $(TRGT)gcc
CP   = $(TRGT)objcopy
AS   = $(TRGT)gcc -x assembler-with-cpp
AR   = $(TRGT)ar
OD   = $(TRGT)objdump
SZ   = $(TRGT)size
HEX  = $(CP) -O ihex
BIN  = $(CP) -O binary

AOPT =
TOPT = -mthumb -DTHUMB
CWARN = -Wall -Wextra -Wundef -Wstrict-prototypes
CPPWARN = -Wall -Wextra -Wundef

##############################################################################
# User defines
##############################################################################

UDEFS = -DCHPRINTF_USE_FLOAT=1 \
        -DIOHDLC_FRAME_PAYLOAD_ALIGNMENT=32U \
        $(IOHDLC_VERSION_DEFS) \
        -DUSE_SPI_ADAPTER \
        -DIOHDLC_ENABLE_STATISTICS \
        -DSHELL_CMD_TEST_ENABLED=FALSE \
        -DSHELL_CMD_FILES_ENABLED=FALSE \
        -DSHELL_CONFIG_FILE

ifeq ($(SPI_MASTER),flexcom2)
  UDEFS += -DTEST_SPI_MASTER_FLEXCOM2
else ifeq ($(SPI_MASTER),spi1)
  UDEFS += -DTEST_SPI_MASTER_SPI1
else
  $(error SPI_MASTER must be flexcom2 or spi1)
endif

UADEFS =
UINCDIR =
ULIBDIR =
ULIBS =

##############################################################################
# Build rules
##############################################################################

RULESPATH = $(CHIBIOS)/os/common/startup/ARMCAx-TZ/compilers/GCC
include $(RULESPATH)/rules.mk

##############################################################################
# Custom targets
##############################################################################

.PHONY: info clean-all prepare-dirs shell shell-spi-flex2 shell-spi1

info:
	@echo "Target:    $(TARGET_LABEL)"
	@echo "ChibiOS:   $(CHIBIOS)"
	@echo "Project:   $(PROJECT)"
	@echo "Output:    $(BUILDDIR)/$(PROJECT).elf"

clean-all: clean
	@rm -rf .dep .dep-* build

prepare-dirs:
	@mkdir -p $(DEPDIR) $(BUILDDIR) $(BUILDDIR)/obj $(BUILDDIR)/lst

PRE_MAKE_ALL_RULE_HOOK: prepare-dirs

shell:
	@$(MAKE) all

shell-spi-flex2:
	@echo "Building MIP104 shell with FLEXCOM2 master and SPI1 slave..."
	@$(MAKE) shell SPI_MASTER=flexcom2 \
		BUILDDIR=build/spi-flex2 DEPDIR=.dep-spi-flex2

shell-spi1:
	@echo "Building MIP104 shell with SPI1 master and FLEXCOM2 slave..."
	@$(MAKE) shell SPI_MASTER=spi1 \
		BUILDDIR=build/spi1 DEPDIR=.dep-spi1
