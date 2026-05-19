##############################################################################
# ioHdlc Test Suite - ChibiOS/ARM frontend for NXP IMX95 SMARC M7
##############################################################################

.DEFAULT_GOAL := shell-spi

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
CHIBIOS ?= $(abspath $(ROOT_DIR)/../ChibiOS)
BUILDDIR := build
DEPDIR := .dep

TARGET_LABEL = IMX95-SMARC-M7

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

ifeq ($(USE_VERBOSE_COMPILE),)
  USE_VERBOSE_COMPILE = no
endif

ifeq ($(USE_SMART_BUILD),)
  USE_SMART_BUILD = yes
endif

##############################################################################
# Architecture specific options
##############################################################################

ifeq ($(USE_PROCESS_STACKSIZE),)
  USE_PROCESS_STACKSIZE = 0x2000
endif

ifeq ($(USE_EXCEPTIONS_STACKSIZE),)
  USE_EXCEPTIONS_STACKSIZE = 0x800
endif

ifeq ($(USE_FPU),)
  USE_FPU = hard
endif

ifeq ($(USE_FPU_OPT),)
  USE_FPU_OPT = -mfloat-abi=$(USE_FPU) -mfpu=fpv5-sp-d16
endif

##############################################################################
# Project, sources and paths
##############################################################################

PROJECT = iohdlc_shell
TEST_MAIN_SRC = $(TEST_CHIBIOS_FRONTEND_DIR)/main_shell.c

include $(ROOT_DIR)/mk/ioHdlc_version.mk
include $(ROOT_DIR)/mk/ioHdlc_sources.mk

MCU = cortex-m7

include $(CHIBIOS)/os/license/license.mk
include $(CHIBIOS)/os/common/startup/ARMCMx/compilers/GCC/mk/startup_mimx95_cm7.mk
include $(CHIBIOS)/os/hal/hal.mk
include $(CHIBIOS)/os/hal/ports/NXP/iMX95/platform.mk
include $(CHIBIOS)/os/hal/boards/IMX_IMX95_SMARC_M7/board.mk
include $(CHIBIOS)/os/hal/osal/rt-nil/osal.mk
include $(CHIBIOS)/os/rt/rt.mk
include $(CHIBIOS)/os/common/ports/ARMv7-M/compilers/GCC/mk/port.mk
include $(CHIBIOS)/os/various/shell/shell.mk
include $(CHIBIOS)/os/hal/lib/streams/streams.mk

ifeq ($(USE_DDR),)
  USE_DDR = yes
endif

ifeq ($(USE_DDR),yes)
  DEMO_USE_DDR = 1
  DEMO_STACKS_DTCM = 0
  LDSCRIPT = $(STARTUPLD)/MIMX9596_M7_DDR.ld
else ifeq ($(USE_DDR),itcm)
  DEMO_USE_DDR = 1
  DEMO_STACKS_DTCM = 1
  LDSCRIPT = $(STARTUPLD)/MIMX9596_M7_ITCM_DDR.ld
else
  DEMO_USE_DDR = 0
  DEMO_STACKS_DTCM = 0
  LDSCRIPT = $(STARTUPLD)/MIMX9596_M7_TCM.ld
endif

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
	$(FRONTEND_ADAPTERS_DIR)/ioHdlcstream_spi_platform.c

COMMON_TEST_SCENARIOS = $(COMMON_SCENARIOS_DIR)/test_exchange.c

CSRC = $(ALLCSRC) \
       $(IOHDLC_CORE_SRCS) \
       $(IOHDLC_CHIBIOS_BASE_SRCS) \
       $(IOHDLC_CHIBIOS_SPI_SRCS) \
       $(CHIBIOS_CONFIG_SRCS) \
       $(COMMON_TEST_SRCS) \
       $(FRONTEND_SRCS) \
       $(COMMON_TEST_SCENARIOS) \
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
         $(FRONTEND_ADAPTERS_DIR) \
         $(BOARD_CONFIG_DIR) \
         $(ALLINC)

##############################################################################
# Compiler settings
##############################################################################

AOPT =
CWARN = -Wall -Wextra -Wundef -Wstrict-prototypes
CPPWARN = -Wall -Wextra -Wundef

##############################################################################
# User defines
##############################################################################

UDEFS = -DCHPRINTF_USE_FLOAT=1 \
        -DMIMX9596 \
        -DCPU_MIMX9596CVZXN_cm7 \
        -DIMX95_DEMO_USE_DDR=$(DEMO_USE_DDR) \
        -DIMX95_DEMO_STACKS_DTCM=$(DEMO_STACKS_DTCM) \
        $(IOHDLC_VERSION_DEFS) \
        -DUSE_SPI_ADAPTER \
        -DIOHDLC_ENABLE_STATISTICS \
        -DSHELL_CMD_TEST_ENABLED=FALSE \
        -DSHELL_CMD_FILES_ENABLED=FALSE \
        -DSHELL_CONFIG_FILE \
        -DIOHDLC_SWDRIVER_SHADOW_TRACE_LINE=LINE_GPIO1_IO14

UADEFS = -DMIMX9596 \
         -DCPU_MIMX9596CVZXN_cm7 \
         -DIMX95_DEMO_USE_DDR=$(DEMO_USE_DDR) \
         -DIMX95_DEMO_STACKS_DTCM=$(DEMO_STACKS_DTCM)
UINCDIR =
ULIBDIR =
ULIBS =

##############################################################################
# Build rules
##############################################################################

ifneq ($(filter yes itcm,$(USE_DDR)),)
override OUTFILES = $(BUILDDIR)/$(PROJECT).elf \
                    $(BUILDDIR)/$(PROJECT).hex \
                    $(BUILDDIR)/$(PROJECT).dmp \
                    $(BUILDDIR)/$(PROJECT).list \
                    $(M7_TCM_BIN) \
                    $(M7_DDR_BIN)

M7_TCM_BIN = $(BUILDDIR)/m7_image_tcm.bin
M7_DDR_BIN = $(BUILDDIR)/m7_image_ddr.bin

M7_TCM_SECTIONS = --only-section=.vectors \
                  --only-section=.itcm_text \
                  --only-section=.itcm_rodata

M7_DDR_SECTIONS = --only-section=.data

ifeq ($(USE_DDR),yes)
M7_DDR_SECTIONS += --only-section=.text \
                   --only-section=.rodata \
                   --only-section=.xtors \
                   --only-section=.ARM.extab \
                   --only-section=.ARM.exidx \
                   --only-section=.eh_frame_hdr \
                   --only-section=.eh_frame
else ifeq ($(USE_DDR),itcm)
M7_TCM_SECTIONS += --only-section=.text \
                   --only-section=.ARM.extab \
                   --only-section=.ARM.exidx \
                   --only-section=.eh_frame_hdr \
                   --only-section=.eh_frame
M7_DDR_SECTIONS += --only-section=.rodata \
                   --only-section=.xtors
endif

$(M7_TCM_BIN) $(M7_DDR_BIN): $(BUILDDIR)/$(PROJECT).elf
	$(CP) -O binary $(M7_TCM_SECTIONS) $< $(M7_TCM_BIN)
	$(CP) -O binary $(M7_DDR_SECTIONS) $< $(M7_DDR_BIN)
endif

RULESPATH = $(CHIBIOS)/os/common/startup/ARMCMx/compilers/GCC/mk
include $(RULESPATH)/arm-none-eabi.mk
include $(RULESPATH)/rules.mk

##############################################################################
# Custom targets
##############################################################################

.PHONY: info shell shell-spi prepare-dirs

prepare-dirs:
	@mkdir -p .dep build build/obj build/lst

PRE_MAKE_ALL_RULE_HOOK: prepare-dirs

info:
	@echo "Building $(PROJECT) for $(TARGET_LABEL)"

shell:
	@echo ""
	@echo "════════════════════════════════════════════════════════"
	@echo "  Building ioHdlc Interactive Shell ($(TARGET_LABEL))"
	@echo "════════════════════════════════════════════════════════"
	@$(MAKE) USE_PROCESS_STACKSIZE=0x2000 all

shell-spi:
	@echo "Building shell with SPI adapter..."
	@$(MAKE) shell
