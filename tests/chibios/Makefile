##############################################################################
# ioHdlc Test Suite - ChibiOS/ARM
# Makefile for compiling tests for ARM Cortex-A5 target
##############################################################################

##############################################################################
# Build global options
##############################################################################

# Compiler options
ifeq ($(USE_OPT),)
  USE_OPT = -Og -ggdb -fomit-frame-pointer -falign-functions=16
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

# Define project name
PROJECT = iohdlc_tests

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

# ChibiOS location
CHIBIOS := $(HOME)/Chibilogic/ChibiOS

MCU = cortex-m4

# Licensing files.
include $(CHIBIOS)/os/license/license.mk

# Startup files.
include $(CHIBIOS)/os/common/startup/ARMCMx/compilers/GCC/mk/startup_stm32f4xx.mk

# HAL-OSAL files (optional).
include $(CHIBIOS)/os/hal/hal.mk
include $(CHIBIOS)/os/hal/ports/STM32/STM32F4xx/platform.mk
include $(CHIBIOS)/os/hal/boards/ST_NUCLEO64_F411RE/board.mk
include $(CHIBIOS)/os/hal/osal/rt-nil/osal.mk

# RTOS files (optional).
include $(CHIBIOS)/os/rt/rt.mk
include $(CHIBIOS)/os/common/ports/ARMv7-M/compilers/GCC/mk/port.mk

# Auto-build files in ./source recursively.
include $(CHIBIOS)/tools/mk/autobuild.mk

# Other files (optional).
include $(CHIBIOS)/os/hal/lib/streams/streams.mk

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
	$(SRC_DIR)/ioHdlcpool_common.c

# Common test infrastructure
COMMON_TEST_SRCS = \
	$(COMMON_DIR)/test_helpers.c \
	$(COMMON_DIR)/test_arenas.c

# Test adapter sources (conditionally set later based on USE_UART_ADAPTER)
TEST_ADAPTER_SRCS =

# OS-agnostic test scenarios (shared with Linux)
COMMON_TEST_SCENARIOS = \
	$(COMMON_SCENARIOS_DIR)/test_frame_pool.c \
	$(COMMON_SCENARIOS_DIR)/test_basic_connection.c \
	$(COMMON_SCENARIOS_DIR)/test_basic_connection_twa.c \
	$(COMMON_SCENARIOS_DIR)/test_checkpoint_tws.c \
	$(COMMON_SCENARIOS_DIR)/test_checkpoint_twa.c

# Test main (combines all tests)
TEST_MAIN_SRC = main_tests.c

# All C sources
CSRC = $(ALLCSRC) \
       $(IOHDLC_CORE_SRCS) \
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
         $(CHIBIOS_DIR) \
         $(CHIBIOS_DIR)/adapters \
         $(CHIBIOS_DIR)/board_config \
         $(ALLINC)

##############################################################################
# Compiler settings
##############################################################################

# Toolchain with absolute path (no PATH dependency)
TRGT = $(HOME)/ARM/bin/arm-none-eabi-
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
UDEFS = -DIOHDLC_USE_CHIBIOS -DCHPRINTF_USE_FLOAT=1 $(CFLAGS_EXTRA)

# Test adapter selection (default: mock)
# Set USE_UART_ADAPTER=1 to build with hardware UART adapter
ifdef USE_UART_ADAPTER
  TEST_ADAPTER_SRCS = adapters/adapter_uart.c
  UDEFS += -DUSE_UART_ADAPTER
else
  TEST_ADAPTER_SRCS = adapters/adapter_mock.c \
                      mocks/mock_stream_chibios.c \
                      mocks/mock_stream_adapter_chibios.c
  UDEFS += -DUSE_MOCK_ADAPTER
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
# Convenience targets for different test configurations
##############################################################################

.PHONY: test-mock test-uart

# Build with mock stream adapter (default, no hardware required)
test-mock:
	@echo "Building with MOCK adapter (memory-based)..."
	$(MAKE) all

# Build with UART hardware adapter (requires physical connections)
test-uart:
	@echo "Building with UART adapter (UARTD2 + FUARTD1)..."
	$(MAKE) all USE_UART_ADAPTER=1
