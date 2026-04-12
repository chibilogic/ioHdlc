##############################################################################
# ioHdlc Test Suite - ChibiOS/ARM
# Makefile for compiling tests for ARM Cortex-A5 target
##############################################################################

##############################################################################
# Build global options
##############################################################################

# Compiler options
ifeq ($(USE_OPT),)
  USE_OPT = -O2 -ggdb -fomit-frame-pointer -falign-functions=16
endif

# C specific options
ifeq ($(USE_COPT),)
  USE_COPT = 
endif

# C++ specific options
ifeq ($(USE_CPPOPT),)
  USE_CPPOPT = -fno-rtti
endif

# Enable linker garbage collection
ifeq ($(USE_LINK_GC),)
  USE_LINK_GC = yes
endif

# Linker extra options
ifeq ($(USE_LDOPT),)
  USE_LDOPT = 
endif

# Enable link time optimizations (LTO)
ifeq ($(USE_LTO),)
  USE_LTO = no
endif

# Enable THUMB mode
ifeq ($(USE_THUMB),)
  USE_THUMB = no
endif

# Enable verbose compile
ifeq ($(USE_VERBOSE_COMPILE),)
  USE_VERBOSE_COMPILE = no
endif

# Enable smart build
ifeq ($(USE_SMART_BUILD),)
  USE_SMART_BUILD = yes
endif

##############################################################################
# Architecture specific options
##############################################################################

# Stack size to be allocated to the Cortex-M process stack. This stack is
# the stack used by the main() thread.
ifeq ($(USE_PROCESS_STACKSIZE),)
  USE_PROCESS_STACKSIZE = 0x400
endif

# Stack size to the allocated to the Cortex-M main/exceptions stack. This
# stack is used for processing interrupts and exceptions.
ifeq ($(USE_EXCEPTIONS_STACKSIZE),)
  USE_EXCEPTIONS_STACKSIZE = 0x400
endif

# Enables the use of FPU (no, softfp, hard).
ifeq ($(USE_FPU),)
  USE_FPU = hard
endif

# FPU-related options.
ifeq ($(USE_FPU_OPT),)
  USE_FPU_OPT = -mfloat-abi=$(USE_FPU) -mfpu=fpv4-sp-d16
endif


##############################################################################
# Project, sources and paths
##############################################################################

# Define project name (changes based on EXCHANGE_TEST or SHELL_TARGET variable)
ifeq ($(SHELL_TARGET),1)
  PROJECT = iohdlc_shell
  TEST_MAIN_SRC = main_shell.c
else ifeq ($(EXCHANGE_TEST),1)
  PROJECT = iohdlc_exchange
  TEST_MAIN_SRC = main_exchange.c
else
  PROJECT = iohdlc_tests
  TEST_MAIN_SRC = main_tests.c
endif

# Paths
ROOT_DIR = ../..
SRC_DIR = $(ROOT_DIR)/src
INCLUDE_DIR = $(ROOT_DIR)/include
OS_CHIBIOS_DIR = $(ROOT_DIR)/os/chibios
TESTS_DIR = ..
CHIBIOS_DIR = .
COMMON_DIR = $(TESTS_DIR)/common
COMMON_SCENARIOS_DIR = $(COMMON_DIR)/scenarios
CONFDIR = ./conf

include $(ROOT_DIR)/mk/ioHdlc_version.mk

# ChibiOS location
CHIBIOS := ../../../ChibiOS

MCU = cortex-m4

# Licensing files.
include $(CHIBIOS)/os/license/license.mk

# Startup files.
include $(CHIBIOS)/os/common/startup/ARMCMx/compilers/GCC/mk/startup_stm32f4xx.mk

# HAL-OSAL files (optional).
include $(CHIBIOS)/os/hal/hal.mk
include $(CHIBIOS)/os/hal/ports/STM32/STM32F4xx/platform.mk
include $(CHIBIOS_DIR)/board_config/board.mk
include $(CHIBIOS)/os/hal/osal/rt-nil/osal.mk

# RTOS files (optional).
include $(CHIBIOS)/os/rt/rt.mk
include $(CHIBIOS)/os/common/ports/ARMv7-M/compilers/GCC/mk/port.mk

# Auto-build files in ./source recursively.
include $(CHIBIOS)/tools/mk/autobuild.mk

# Other files (optional).
include $(CHIBIOS)/os/hal/lib/streams/streams.mk

# Shell (optional, needed for shell target)
ifeq ($(SHELL_TARGET),1)
  include $(CHIBIOS)/os/various/shell/shell.mk
endif

# ioHdlc ChibiOS binding
include $(OS_CHIBIOS_DIR)/ioHdlcBinding.mk

# Define linker script file here
LDSCRIPT= $(STARTUPLD)/STM32F411xE.ld

##############################################################################
# Source files
##############################################################################

# ioHdlc core sources
IOHDLC_CORE_SRCS = \
	$(SRC_DIR)/ioHdlc_core.c \
	$(SRC_DIR)/ioHdlc.c \
	$(SRC_DIR)/ioHdlc_log.c \
	$(SRC_DIR)/ioHdlcll.c \
	$(SRC_DIR)/ioHdlcswdriver.c \
	$(SRC_DIR)/ioHdlcpool_common.c \
	$(SRC_DIR)/ioHdlc_runner.c

# ChibiOS platform-specific config
ifeq ($(SHELL_TARGET),1)
  # Shell: use runtime CLI parameter parsing
  CHIBIOS_CONFIG_SRCS = \
	$(CHIBIOS_DIR)/test_config_shell.c
else
  # Tests/Exchange: use compile-time configuration
  CHIBIOS_CONFIG_SRCS = \
	$(CHIBIOS_DIR)/test_config_chibios.c
endif

# Common test infrastructure
COMMON_TEST_SRCS = \
	$(COMMON_DIR)/test_framework.c \
	$(COMMON_DIR)/test_helpers.c \
	$(COMMON_DIR)/test_arenas.c

# Test adapter sources (conditionally set later based on USE_UART_ADAPTER)
TEST_ADAPTER_SRCS =

# OS-agnostic test scenarios (shared with Linux)
ifeq ($(SHELL_TARGET),1)
  # Shell target needs all test scenarios for interactive commands
  COMMON_TEST_SCENARIOS = \
	$(COMMON_SCENARIOS_DIR)/test_frame_pool.c \
	$(COMMON_SCENARIOS_DIR)/test_basic_connection.c \
	$(COMMON_SCENARIOS_DIR)/test_basic_connection_twa.c \
	$(COMMON_SCENARIOS_DIR)/test_checkpoint_tws.c \
	$(COMMON_SCENARIOS_DIR)/test_checkpoint_twa.c \
	$(COMMON_SCENARIOS_DIR)/test_exchange.c
else ifeq ($(EXCHANGE_TEST),1)
  # Exchange test only needs test_exchange.c
  COMMON_TEST_SCENARIOS = \
	$(COMMON_SCENARIOS_DIR)/test_exchange.c
else
  # Unit tests use all test scenarios
  COMMON_TEST_SCENARIOS = \
	$(COMMON_SCENARIOS_DIR)/test_frame_pool.c \
	$(COMMON_SCENARIOS_DIR)/test_basic_connection.c \
	$(COMMON_SCENARIOS_DIR)/test_basic_connection_twa.c \
	$(COMMON_SCENARIOS_DIR)/test_checkpoint_tws.c \
	$(COMMON_SCENARIOS_DIR)/test_checkpoint_twa.c
endif

# All C sources
CSRC = $(ALLCSRC) \
       $(IOHDLC_CORE_SRCS) \
       $(CHIBIOS_CONFIG_SRCS) \
       $(COMMON_TEST_SRCS) \
       $(TEST_ADAPTER_SRCS) \
       $(COMMON_TEST_SCENARIOS) \
       $(TEST_MAIN_SRC)

# C++ sources
CPPSRC = $(ALLCPPSRC)

# ARM mode sources
ACSRC =
ACPPSRC =

# THUMB mode sources
TCSRC =
TCPPSRC =

# ASM sources
ASMSRC = $(ALLASMSRC)
ASMXSRC = $(ALLXASMSRC)

# Include directories
INCDIR = $(CONFDIR) \
         $(INCLUDE_DIR) \
         $(OS_CHIBIOS_DIR)/include \
         $(COMMON_DIR) \
         $(COMMON_DIR)/mocks \
         $(COMMON_DIR)/adapters \
         $(CHIBIOS_DIR) \
         $(CHIBIOS_DIR)/adapters \
         $(ALLINC)

##############################################################################
# Compiler settings
##############################################################################

# ARM-specific options
AOPT =

# C warning options
CWARN = -Wall -Wextra -Wundef -Wstrict-prototypes

# C++ warning options
CPPWARN = -Wall -Wextra -Wundef

##############################################################################
# User defines
##############################################################################

# User C defines
UDEFS = -DCHPRINTF_USE_FLOAT=1 $(IOHDLC_VERSION_DEFS) $(CFLAGS_EXTRA)

# Shell command configuration (when building shell target)
ifeq ($(SHELL_TARGET),1)
  UDEFS += -DSHELL_CMD_TEST_ENABLED=FALSE
  UDEFS += -DSHELL_CMD_FILES_ENABLED=FALSE
  UDEFS += -DSHELL_CONFIG_FILE
endif

# Test adapter selection (default: mock)
# Set USE_UART_ADAPTER=1 to build with hardware UART adapter
# Set USE_SPI_ADAPTER=1  to build with hardware SPI adapter
ifdef USE_UART_ADAPTER
  TEST_ADAPTER_SRCS = adapters/adapter_uart.c
  UDEFS += -DUSE_UART_ADAPTER -DIOHDLC_ENABLE_STATISTICS
else ifdef USE_SPI_ADAPTER
  TEST_ADAPTER_SRCS = adapters/adapter_spi.c
  UDEFS += -DUSE_SPI_ADAPTER -DIOHDLC_SPI_USE_DR -DIOHDLC_ENABLE_STATISTICS
else
  TEST_ADAPTER_SRCS = $(COMMON_DIR)/adapters/adapter_mock.c \
                      $(COMMON_DIR)/mocks/mock_stream.c \
                      $(COMMON_DIR)/mocks/mock_stream_adapter.c
  UDEFS += -DUSE_MOCK_ADAPTER -DIOHDLC_ENABLE_STATISTICS
endif

# ASM defines
UADEFS = 

# User include directories
UINCDIR =

# User library directories
ULIBDIR =

# User libraries
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

.PHONY: info clean-all

# Display build information
info:
	@echo "════════════════════════════════════════════════════════"
	@echo "  ioHdlc Test Suite - ChibiOS/ARM Build"
	@echo "════════════════════════════════════════════════════════"
	@echo "Target:      ARM Cortex-M (F411RE)"
	@echo "Toolchain:   $(CC)"
	@echo "ChibiOS:     $(CHIBIOS)"
	@echo "Project:     $(PROJECT)"
	@echo ""
	@echo "Test Sources:"
	@echo "  - Frame Pool Tests (common)"
	@echo "  - Basic Connection Tests (common)"
	@echo ""
	@echo "Output:      build/$(PROJECT).elf"
	@echo "             build/$(PROJECT).bin"
	@echo "             build/$(PROJECT).hex"
	@echo "════════════════════════════════════════════════════════"

# Clean everything including ChibiOS build artifacts
clean-all: clean
	@echo "Cleaning all build artifacts..."
	@rm -rf .dep build lst

##############################################################################
# Convenience targets for different configurations
##############################################################################

.PHONY: tests exchange shell test-mock test-uart test-spi shell-mock shell-uart shell-spi

# Build unit tests suite (main_tests.c)
tests:
	@echo ""
	@echo "════════════════════════════════════════════════════════"
	@echo "  Building ioHdlc Unit Tests Suite"
	@echo "════════════════════════════════════════════════════════"
	@$(MAKE) EXCHANGE_TEST=0 SHELL_TARGET=0 all

# Build exchange test (main_exchange.c)
exchange:
	@echo ""
	@echo "════════════════════════════════════════════════════════"
	@echo "  Building ioHdlc Exchange Test"
	@echo "════════════════════════════════════════════════════════"
	@$(MAKE) EXCHANGE_TEST=1 SHELL_TARGET=0 all

# Build interactive shell (main_shell.c)
shell:
	@echo ""
	@echo "════════════════════════════════════════════════════════"
	@echo "  Building ioHdlc Interactive Shell"
	@echo "════════════════════════════════════════════════════════"
	@$(MAKE) USE_PROCESS_STACKSIZE=0x2000 SHELL_TARGET=1 USE_UART_ADAPTER=$(USE_UART_ADAPTER) all

# Build with mock stream adapter (default, no hardware required)
test-mock:
	@echo "Building tests with MOCK adapter (memory-based)..."
	@$(MAKE) tests

# Build with UART hardware adapter (requires physical connections)
test-uart:
	@echo "Building tests with UART adapter (UARTD2 + FUART D1)..."
	@$(MAKE) tests USE_UART_ADAPTER=1

# Build with SPI hardware adapter (requires physical connections)
test-spi:
	@echo "Building tests with SPI adapter (SPID1 master + SPID2 slave)..."
	@$(MAKE) tests USE_SPI_ADAPTER=1

# Build shell with mock stream adapter (default)
shell-mock:
	@echo "Building shell with MOCK adapter (memory-based)..."
	@$(MAKE) shell

# Build shell with UART hardware adapter
shell-uart:
	@echo "Building shell with UART adapter ..."
	@$(MAKE) shell USE_UART_ADAPTER=1

# Build shell with SPI hardware adapter
shell-spi:
	@echo "Building shell with SPI adapter (SPID1 master + SPID2 slave)..."
	@$(MAKE) shell USE_SPI_ADAPTER=1
