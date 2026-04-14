# ioHdlc Test Suite - Linux
# Makefile for compiling and running tests on Linux/POSIX

# Paths
ROOT_DIR = ../..
SRC_DIR = $(ROOT_DIR)/src
INCLUDE_DIR = $(ROOT_DIR)/include
OS_LINUX_DIR = $(ROOT_DIR)/os/linux
TESTS_DIR = ..
LINUX_DIR = .
MOCKS_DIR = $(COMMON_DIR)/mocks
SCENARIOS_DIR = $(LINUX_DIR)/scenarios
COMMON_DIR = $(TESTS_DIR)/common
COMMON_SCENARIOS_DIR = $(COMMON_DIR)/scenarios

include $(ROOT_DIR)/mk/ioHdlc_version.mk

# Output
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
BIN_DIR = $(BUILD_DIR)/bin

# Compiler (force native, not cross-compiler)
CC = /usr/bin/gcc
CFLAGS = -Wall -Wextra -std=c11 -g3 -O2
CFLAGS += -D_XOPEN_SOURCE=600 -D_GNU_SOURCE
CFLAGS += -pthread
CFLAGS += -I$(INCLUDE_DIR)
CFLAGS += -I$(OS_LINUX_DIR)/include
CFLAGS += -I$(MOCKS_DIR)
CFLAGS += -I$(COMMON_DIR)
CFLAGS += -I$(COMMON_DIR)/adapters
CFLAGS += -I$(LINUX_DIR)
CFLAGS += -DIOHDLC_USE_POSIX
CFLAGS += -DIOHDLC_ENABLE_STATISTICS
CFLAGS += $(IOHDLC_VERSION_DEFS)
CFLAGS += $(CFLAGS_EXTRA)

# Generate header dependency files next to object files.
DEPFLAGS = -MMD -MP

# Linker flags
LDFLAGS = -pthread -lrt

# Source files
CORE_SRCS = \
	$(SRC_DIR)/ioHdlc_core.c \
	$(SRC_DIR)/ioHdlc.c \
	$(SRC_DIR)/ioHdlc_log.c \
	$(SRC_DIR)/ioHdlcll.c \
	$(SRC_DIR)/ioHdlcswdriver.c \
	$(SRC_DIR)/ioHdlcpool_common.c \
	$(SRC_DIR)/ioHdlc_runner.c

OSAL_SRCS = \
	$(OS_LINUX_DIR)/src/ioHdlcosal.c \
	$(OS_LINUX_DIR)/src/ioHdlcfmempool.c

MOCK_SRCS = \
	$(MOCKS_DIR)/mock_stream.c \
	$(MOCKS_DIR)/mock_stream_adapter.c \
	$(MOCKS_DIR)/mock_bus.c

ADAPTER_SRCS = \
	$(COMMON_DIR)/adapters/adapter_mock.c

LINUX_CONFIG_SRCS = \
	$(LINUX_DIR)/test_config_linux.c

COMMON_SRCS = \
	$(COMMON_DIR)/test_helpers.c \
	$(COMMON_DIR)/test_arenas.c \
	$(COMMON_DIR)/test_framework.c

# OS-agnostic test scenarios (shared across all platforms)
COMMON_TEST_SRCS = \
	$(COMMON_SCENARIOS_DIR)/test_frame_pool.c \
	$(COMMON_SCENARIOS_DIR)/test_basic_connection.c \
	$(COMMON_SCENARIOS_DIR)/test_basic_connection_twa.c \
	$(COMMON_SCENARIOS_DIR)/test_checkpoint_tws.c \
	$(COMMON_SCENARIOS_DIR)/test_checkpoint_twa.c \
	$(COMMON_SCENARIOS_DIR)/test_exchange.c

# Linux-specific test scenarios
LINUX_TEST_SRCS = \
	$(SCENARIOS_DIR)/test_mock_stream.c \
	$(SCENARIOS_DIR)/test_osal_bsem.c \
	$(SCENARIOS_DIR)/test_osal_events.c

# Test scenarios
TEST_MOCK_STREAM = test_mock_stream
TEST_BASIC_CONNECTION = test_basic_connection
TEST_BASIC_CONNECTION_TWA = test_basic_connection_twa
TEST_EXCHANGE = test_exchange
TEST_FRAME_POOL = test_frame_pool
TEST_CHECKPOINT_TWS = test_checkpoint_tws
TEST_CHECKPOINT_TWA = test_checkpoint_twa
TEST_FRMR = test_frmr
TEST_MULTIPOINT_NRM = test_multipoint_nrm
TEST_BASIC_CONNECTION_ABM = test_basic_connection_abm
TEST_MOD128_ABM = test_mod128_abm
TEST_OSAL_BSEM = test_osal_bsem
TEST_OSAL_EVENTS = test_osal_events

# Object files
CORE_OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(CORE_SRCS))
OSAL_OBJS = $(patsubst $(OS_LINUX_DIR)/src/%.c,$(OBJ_DIR)/osal_%.o,$(OSAL_SRCS))
MOCK_OBJS = $(patsubst $(MOCKS_DIR)/%.c,$(OBJ_DIR)/mock_%.o,$(MOCK_SRCS))
ADAPTER_OBJS = $(patsubst $(COMMON_DIR)/adapters/%.c,$(OBJ_DIR)/%.o,$(ADAPTER_SRCS))
LINUX_CONFIG_OBJS = $(patsubst $(LINUX_DIR)/%.c,$(OBJ_DIR)/%.o,$(LINUX_CONFIG_SRCS))
COMMON_OBJS = $(patsubst $(COMMON_DIR)/%.c,$(OBJ_DIR)/common_%.o,$(COMMON_SRCS))

# Common test scenario objects (OS-agnostic test logic)
COMMON_TEST_OBJS = $(patsubst $(COMMON_SCENARIOS_DIR)/%.c,$(OBJ_DIR)/%.o,$(COMMON_TEST_SRCS))

# Minimal OSAL for tests that only need time functions (test_framework dependency)
OSAL_MINIMAL = $(OBJ_DIR)/osal_ioHdlcosal.o

ALL_OBJS = $(CORE_OBJS) $(OSAL_OBJS) $(MOCK_OBJS) $(ADAPTER_OBJS) $(LINUX_CONFIG_OBJS) $(COMMON_OBJS)

# Test binaries
AUTO_TEST_BINS = $(BIN_DIR)/$(TEST_MOCK_STREAM)
AUTO_TEST_BINS += $(BIN_DIR)/$(TEST_BASIC_CONNECTION)
AUTO_TEST_BINS += $(BIN_DIR)/$(TEST_BASIC_CONNECTION_TWA)
AUTO_TEST_BINS += $(BIN_DIR)/$(TEST_FRAME_POOL)
AUTO_TEST_BINS += $(BIN_DIR)/$(TEST_CHECKPOINT_TWS)
AUTO_TEST_BINS += $(BIN_DIR)/$(TEST_CHECKPOINT_TWA)
AUTO_TEST_BINS += $(BIN_DIR)/$(TEST_FRMR)
AUTO_TEST_BINS += $(BIN_DIR)/$(TEST_MULTIPOINT_NRM)
AUTO_TEST_BINS += $(BIN_DIR)/$(TEST_OSAL_BSEM)
AUTO_TEST_BINS += $(BIN_DIR)/$(TEST_OSAL_EVENTS)
AUTO_TEST_BINS += $(BIN_DIR)/$(TEST_BASIC_CONNECTION_ABM)
AUTO_TEST_BINS += $(BIN_DIR)/$(TEST_MOD128_ABM)

# Manual test tools (require command-line parameters)
MANUAL_TEST_BINS = $(BIN_DIR)/$(TEST_EXCHANGE)

# All test binaries
TEST_BINS = $(AUTO_TEST_BINS) $(MANUAL_TEST_BINS)

# Targets
.PHONY: all clean test test-osal dirs

all: dirs $(TEST_BINS)

dirs:
	@mkdir -p $(OBJ_DIR) $(BIN_DIR)

# Compile core sources
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | dirs
	@echo "CC $<"
	@$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Compile OSAL sources
$(OBJ_DIR)/osal_%.o: $(OS_LINUX_DIR)/src/%.c | dirs
	@echo "CC $<"
	@$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Compile mock sources
$(OBJ_DIR)/mock_%.o: $(MOCKS_DIR)/%.c | dirs
	@echo "CC $<"
	@$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Compile adapter sources (from common/adapters)
$(OBJ_DIR)/adapter_%.o: $(COMMON_DIR)/adapters/adapter_%.c | dirs
	@echo "CC $< (adapter)"
	@$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Compile platform config sources
$(OBJ_DIR)/test_config_%.o: $(LINUX_DIR)/test_config_%.c | dirs
	@echo "CC $< (config)"
	@$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Compile common sources
$(OBJ_DIR)/common_%.o: $(COMMON_DIR)/%.c | dirs
	@echo "CC $<"
	@$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Compile common test scenarios (OS-agnostic)
$(OBJ_DIR)/test_%.o: $(COMMON_SCENARIOS_DIR)/test_%.c | dirs
	@echo "CC $< (common)"
	@$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Compile Linux-specific test scenarios
$(OBJ_DIR)/test_%.o: $(SCENARIOS_DIR)/test_%.c | dirs
	@echo "CC $< (linux)"
	@$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Compile Linux test programs (in current directory)
$(OBJ_DIR)/test_%.o: $(LINUX_DIR)/test_%.c | dirs
	@echo "CC $< (linux)"
	@$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Compile test runners (Linux-specific entry points)
$(OBJ_DIR)/test_runner_%.o: $(LINUX_DIR)/test_runner_%.c | dirs
	@echo "CC $< (runner)"
	@$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Link test binaries
$(BIN_DIR)/$(TEST_MOCK_STREAM): $(OBJ_DIR)/test_mock_stream.o $(MOCK_OBJS) $(COMMON_OBJS) $(OSAL_MINIMAL) $(OBJ_DIR)/ioHdlcll.o
	@echo "LD $@"
	@$(CC) $^ -o $@ $(LDFLAGS)

# Link test binaries with runners
$(BIN_DIR)/$(TEST_BASIC_CONNECTION): $(OBJ_DIR)/test_runner_basic_connection.o $(OBJ_DIR)/test_basic_connection.o $(ALL_OBJS)
	@echo "LD $@"
	@$(CC) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(TEST_BASIC_CONNECTION_TWA): $(OBJ_DIR)/test_runner_basic_connection_twa.o $(OBJ_DIR)/test_basic_connection_twa.o $(ALL_OBJS)
	@echo "LD $@"
	@$(CC) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(TEST_EXCHANGE): $(OBJ_DIR)/test_runner_exchange.o $(OBJ_DIR)/test_exchange.o $(ALL_OBJS)
	@echo "LD $@"
	@$(CC) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(TEST_FRAME_POOL): $(OBJ_DIR)/test_runner_frame_pool.o $(OBJ_DIR)/test_frame_pool.o $(ALL_OBJS)
	@echo "LD $@"
	@$(CC) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(TEST_CHECKPOINT_TWS): $(OBJ_DIR)/test_runner_checkpoint_tws.o $(OBJ_DIR)/test_checkpoint_tws.o $(ALL_OBJS)
	@echo "LD $@"
	@$(CC) $^ -o $@ $(LDFLAGS)
	
$(BIN_DIR)/$(TEST_CHECKPOINT_TWA): $(OBJ_DIR)/test_runner_checkpoint_twa.o $(OBJ_DIR)/test_checkpoint_twa.o $(ALL_OBJS)
	@echo "LD $@"
	@$(CC) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(TEST_FRMR): $(OBJ_DIR)/test_runner_frmr.o $(OBJ_DIR)/test_frmr.o $(ALL_OBJS)
	@echo "LD $@"
	@$(CC) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(TEST_MULTIPOINT_NRM): $(OBJ_DIR)/test_runner_multipoint_nrm.o $(OBJ_DIR)/test_multipoint_nrm.o $(ALL_OBJS)
	@echo "LD $@"
	@$(CC) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(TEST_BASIC_CONNECTION_ABM): $(OBJ_DIR)/test_runner_basic_connection_abm.o $(OBJ_DIR)/test_basic_connection_abm.o $(ALL_OBJS)
	@echo "LD $@"
	@$(CC) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(TEST_MOD128_ABM): $(OBJ_DIR)/test_runner_mod128_abm.o $(OBJ_DIR)/test_mod128_abm.o $(ALL_OBJS)
	@echo "LD $@"
	@$(CC) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(TEST_OSAL_BSEM): $(OBJ_DIR)/test_osal_bsem.o $(OSAL_MINIMAL) $(COMMON_OBJS)
	@echo "LD $@"
	@$(CC) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(TEST_OSAL_EVENTS): $(OBJ_DIR)/test_osal_events.o $(OSAL_MINIMAL) $(COMMON_OBJS)
	@echo "LD $@"
	@$(CC) $^ -o $@ $(LDFLAGS)

# Run tests
test: all
	@echo ""
	@echo "════════════════════════════════════════════════════════"
	@echo "  Running Test Suite"
	@echo "════════════════════════════════════════════════════════"
	@echo ""
	@for test in $(AUTO_TEST_BINS); do \
		$$test || exit 1; \
	done
	@echo ""
	@echo "════════════════════════════════════════════════════════"
	@echo "  All Tests Completed Successfully"
	@echo "════════════════════════════════════════════════════════"
	@echo ""
	@echo "Manual test tools (require parameters):"
	@for test in $(MANUAL_TEST_BINS); do \
		echo "  $$test --help"; \
	done
	@echo ""

# Run OSAL tests only
test-osal: $(BIN_DIR)/$(TEST_OSAL_BSEM) $(BIN_DIR)/$(TEST_OSAL_EVENTS)
	@echo ""
	@echo "════════════════════════════════════════════════════════"
	@echo "  Running OSAL Tests"
	@echo "════════════════════════════════════════════════════════"
	@echo ""
	@$(BIN_DIR)/$(TEST_OSAL_BSEM) || exit 1
	@$(BIN_DIR)/$(TEST_OSAL_EVENTS) || exit 1
	@echo ""
	@echo "════════════════════════════════════════════════════════"
	@echo "  OSAL Tests Completed Successfully"
	@echo "════════════════════════════════════════════════════════"
	@echo ""

# Clean
clean:
	@echo "Cleaning build directory..."
	@rm -rf $(BUILD_DIR)

-include $(wildcard $(OBJ_DIR)/*.d)

# Help
help:
	@echo "ioHdlc Linux Test Suite"
	@echo ""
	@echo "Targets:"
	@echo "  all       - Build all test binaries"
	@echo "  test      - Build and run all automated tests"
	@echo "  test-osal - Build and run OSAL tests only"
	@echo "  clean     - Remove build artifacts"
	@echo "  help      - Show this message"
	@echo ""
	@echo "Examples:"
	@echo "  make test                                    # Run all tests"
	@echo "  make test-osal                               # Run OSAL tests only"
	@echo "  make clean && make                           # Rebuild from scratch"
	@echo "  make CFLAGS_EXTRA=\"-DIOHDLC_LOG_LEVEL=1\"     # Enable frame logging"
	@echo ""
	@echo "Manual test tools (require command-line parameters):"
	@echo "  build/bin/test_exchange --help               # Parametrized HDLC testing"
	@echo ""
	@echo "Logging levels (IOHDLC_LOG_LEVEL):"
	@echo "  0 - OFF (default, zero overhead)"
	@echo "  1 - FRAMES (frame headers only)"
	@echo "  2 - DATA (headers + partial payload)"
	@echo "  3 - FULL (headers + complete hex dump)"
