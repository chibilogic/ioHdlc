##############################################################################
# ioHdlc Test Suite - ChibiOS/ARM frontend for STM32 NUCLEO64-G474RE
##############################################################################

.DEFAULT_GOAL := tests

TEST_CHIBIOS_FRONTEND_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
TESTS_CHIBIOS_DIR := $(abspath $(TEST_CHIBIOS_FRONTEND_DIR)/..)
ROOT_DIR := $(abspath $(TESTS_CHIBIOS_DIR)/../..)
SRC_DIR := $(ROOT_DIR)/src
INCLUDE_DIR := $(ROOT_DIR)/include
OS_CHIBIOS_DIR := $(ROOT_DIR)/os/chibios
COMMON_DIR := $(TESTS_CHIBIOS_DIR)/../common
COMMON_SCENARIOS_DIR := $(COMMON_DIR)/scenarios
CONFDIR := $(TEST_CHIBIOS_FRONTEND_DIR)/conf
CHIBIOS ?= $(abspath $(ROOT_DIR)/../ChibiOS)

TARGET_LABEL = G474RE
TARGET_STARTUP_MK_NAME = startup_stm32g4xx.mk
TARGET_PLATFORM_MK_REL = os/hal/ports/STM32/STM32G4xx/platform.mk
TARGET_LDSCRIPT = $(TEST_CHIBIOS_FRONTEND_DIR)/STM32G474xE_iohdlc.ld
TARGET_MCU_DEFINE = STM32G474xx

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
  USE_SMART_BUILD = no
endif

##############################################################################
# Architecture specific options
##############################################################################

ifeq ($(USE_PROCESS_STACKSIZE),)
  USE_PROCESS_STACKSIZE = 0x400
endif

ifeq ($(USE_EXCEPTIONS_STACKSIZE),)
  USE_EXCEPTIONS_STACKSIZE = 0x400
endif

ifeq ($(USE_FPU),)
  USE_FPU = hard
endif

ifeq ($(USE_FPU_OPT),)
  USE_FPU_OPT = -mfloat-abi=$(USE_FPU) -mfpu=fpv4-sp-d16
endif

##############################################################################
# Project, sources and paths
##############################################################################

ifeq ($(SHELL_TARGET),1)
  PROJECT = iohdlc_shell
  TEST_MAIN_SRC = $(TESTS_CHIBIOS_DIR)/main_shell.c
else ifeq ($(EXCHANGE_TEST),1)
  PROJECT = iohdlc_exchange
  TEST_MAIN_SRC = $(TESTS_CHIBIOS_DIR)/main_exchange.c
else
  PROJECT = iohdlc_tests
  TEST_MAIN_SRC = $(TESTS_CHIBIOS_DIR)/main_tests.c
endif

include $(ROOT_DIR)/mk/ioHdlc_version.mk

MCU = cortex-m4

include $(CHIBIOS)/os/license/license.mk
include $(CHIBIOS)/os/common/startup/ARMCMx/compilers/GCC/mk/$(TARGET_STARTUP_MK_NAME)
include $(CHIBIOS)/os/hal/hal.mk
include $(CHIBIOS)/$(TARGET_PLATFORM_MK_REL)
include $(TEST_CHIBIOS_FRONTEND_DIR)/board_config/board.mk
include $(CHIBIOS)/os/hal/osal/rt-nil/osal.mk
include $(CHIBIOS)/os/rt/rt.mk
include $(CHIBIOS)/os/common/ports/ARMv7-M/compilers/GCC/mk/port.mk
include $(CHIBIOS)/tools/mk/autobuild.mk
include $(CHIBIOS)/os/hal/lib/streams/streams.mk

ifeq ($(SHELL_TARGET),1)
  include $(CHIBIOS)/os/various/shell/shell.mk
endif

include $(OS_CHIBIOS_DIR)/ioHdlcBinding.mk

LDSCRIPT = $(TARGET_LDSCRIPT)

##############################################################################
# Source files
##############################################################################

IOHDLC_CORE_SRCS = \
	$(SRC_DIR)/ioHdlc_core.c \
	$(SRC_DIR)/ioHdlc.c \
	$(SRC_DIR)/ioHdlc_log.c \
	$(SRC_DIR)/ioHdlcll.c \
	$(SRC_DIR)/ioHdlcswdriver.c \
	$(SRC_DIR)/ioHdlcpool_common.c \
	$(SRC_DIR)/ioHdlc_runner.c

ifeq ($(SHELL_TARGET),1)
  CHIBIOS_CONFIG_SRCS = $(TESTS_CHIBIOS_DIR)/test_config_shell.c
else
  CHIBIOS_CONFIG_SRCS = $(TESTS_CHIBIOS_DIR)/test_config_chibios.c
endif

COMMON_TEST_SRCS = \
	$(COMMON_DIR)/test_framework.c \
	$(COMMON_DIR)/test_helpers.c \
	$(COMMON_DIR)/test_arenas.c

TEST_ADAPTER_SRCS =

ifeq ($(SHELL_TARGET),1)
  COMMON_TEST_SCENARIOS = \
	$(COMMON_SCENARIOS_DIR)/test_frame_pool.c \
	$(COMMON_SCENARIOS_DIR)/test_basic_connection.c \
	$(COMMON_SCENARIOS_DIR)/test_basic_connection_twa.c \
	$(COMMON_SCENARIOS_DIR)/test_checkpoint_tws.c \
	$(COMMON_SCENARIOS_DIR)/test_checkpoint_twa.c \
	$(COMMON_SCENARIOS_DIR)/test_exchange.c
else ifeq ($(EXCHANGE_TEST),1)
  COMMON_TEST_SCENARIOS = $(COMMON_SCENARIOS_DIR)/test_exchange.c
else
  COMMON_TEST_SCENARIOS = \
	$(COMMON_SCENARIOS_DIR)/test_frame_pool.c \
	$(COMMON_SCENARIOS_DIR)/test_basic_connection.c \
	$(COMMON_SCENARIOS_DIR)/test_basic_connection_twa.c \
	$(COMMON_SCENARIOS_DIR)/test_checkpoint_tws.c \
	$(COMMON_SCENARIOS_DIR)/test_checkpoint_twa.c
endif

CSRC = $(ALLCSRC) \
       $(IOHDLC_CORE_SRCS) \
       $(CHIBIOS_CONFIG_SRCS) \
       $(COMMON_TEST_SRCS) \
       $(TEST_ADAPTER_SRCS) \
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
         $(COMMON_DIR)/mocks \
         $(COMMON_DIR)/adapters \
         $(TESTS_CHIBIOS_DIR) \
         $(TESTS_CHIBIOS_DIR)/adapters \
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

UDEFS = -DCHPRINTF_USE_FLOAT=1 -D$(TARGET_MCU_DEFINE) $(IOHDLC_VERSION_DEFS) $(CFLAGS_EXTRA)

ifeq ($(SHELL_TARGET),1)
  UDEFS += -DSHELL_CMD_TEST_ENABLED=FALSE
  UDEFS += -DSHELL_CMD_FILES_ENABLED=FALSE
  UDEFS += -DSHELL_CONFIG_FILE
endif

ifdef USE_UART_ADAPTER
  TEST_ADAPTER_SRCS = $(TESTS_CHIBIOS_DIR)/adapters/adapter_uart.c
  UDEFS += -DUSE_UART_ADAPTER -DIOHDLC_ENABLE_STATISTICS
else ifdef USE_SPI_ADAPTER
  TEST_ADAPTER_SRCS = $(TESTS_CHIBIOS_DIR)/adapters/adapter_spi.c
  UDEFS += -DUSE_SPI_ADAPTER -DIOHDLC_SPI_USE_DR -DIOHDLC_ENABLE_STATISTICS
else
  TEST_ADAPTER_SRCS = $(COMMON_DIR)/adapters/adapter_mock.c \
                      $(COMMON_DIR)/mocks/mock_stream.c \
                      $(COMMON_DIR)/mocks/mock_stream_adapter.c
  UDEFS += -DUSE_MOCK_ADAPTER -DIOHDLC_ENABLE_STATISTICS
endif

UADEFS =
UINCDIR =
ULIBDIR =
ULIBS =

##############################################################################
# Build rules
##############################################################################

RULESPATH = $(CHIBIOS)/os/common/startup/ARMCMx/compilers/GCC/mk
include $(RULESPATH)/arm-none-eabi.mk
include $(RULESPATH)/rules.mk

##############################################################################
# Custom targets
##############################################################################

.PHONY: info clean-all prepare-dirs \
        tests exchange shell test-mock test-uart test-spi \
        shell-mock shell-uart shell-spi

prepare-dirs:
	@mkdir -p .dep build build/obj build/lst

PRE_MAKE_ALL_RULE_HOOK: prepare-dirs

info:
	@echo "════════════════════════════════════════════════════════"
	@echo "  ioHdlc Test Suite - ChibiOS/ARM Build"
	@echo "════════════════════════════════════════════════════════"
	@echo "Target:      ARM Cortex-M ($(TARGET_LABEL))"
	@echo "Toolchain:   $(CC)"
	@echo "ChibiOS:     $(CHIBIOS)"
	@echo "Project:     $(PROJECT)"
	@echo ""
	@echo "Output:      build/$(PROJECT).elf"
	@echo "             build/$(PROJECT).bin"
	@echo "             build/$(PROJECT).hex"
	@echo "════════════════════════════════════════════════════════"

clean-all: clean
	@echo "Cleaning all build artifacts..."
	@rm -rf .dep build lst

tests:
	@echo ""
	@echo "════════════════════════════════════════════════════════"
	@echo "  Building ioHdlc Unit Tests Suite ($(TARGET_LABEL))"
	@echo "════════════════════════════════════════════════════════"
	@$(MAKE) EXCHANGE_TEST=0 SHELL_TARGET=0 all

exchange:
	@echo ""
	@echo "════════════════════════════════════════════════════════"
	@echo "  Building ioHdlc Exchange Test ($(TARGET_LABEL))"
	@echo "════════════════════════════════════════════════════════"
	@$(MAKE) EXCHANGE_TEST=1 SHELL_TARGET=0 all

shell:
	@echo ""
	@echo "════════════════════════════════════════════════════════"
	@echo "  Building ioHdlc Interactive Shell ($(TARGET_LABEL))"
	@echo "════════════════════════════════════════════════════════"
	@$(MAKE) USE_PROCESS_STACKSIZE=0x2000 SHELL_TARGET=1 all

test-mock:
	@echo "Building tests with MOCK adapter (memory-based)..."
	@$(MAKE) tests

test-uart:
	@echo "Building tests with UART adapter..."
	@$(MAKE) tests USE_UART_ADAPTER=1

test-spi:
	@echo "Building tests with SPI adapter..."
	@$(MAKE) tests USE_SPI_ADAPTER=1

shell-mock:
	@echo "Building shell with MOCK adapter (memory-based)..."
	@$(MAKE) shell

shell-uart:
	@echo "Building shell with UART adapter..."
	@$(MAKE) shell USE_UART_ADAPTER=1

shell-spi:
	@echo "Building shell with SPI adapter..."
	@$(MAKE) shell USE_SPI_ADAPTER=1
