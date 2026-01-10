# ioHdlc Test Suite - Linux
# Makefile for compiling and running tests on Linux/POSIX

# Paths
ROOT_DIR = ../..
SRC_DIR = $(ROOT_DIR)/src
INCLUDE_DIR = $(ROOT_DIR)/include
OS_LINUX_DIR = $(ROOT_DIR)/os/linux
TESTS_DIR = ..
LINUX_DIR = .
MOCKS_DIR = $(LINUX_DIR)/mocks
SCENARIOS_DIR = $(LINUX_DIR)/scenarios
COMMON_DIR = $(TESTS_DIR)/common
COMMON_SCENARIOS_DIR = $(COMMON_DIR)/scenarios

# Output
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
BIN_DIR = $(BUILD_DIR)/bin

# Compiler (force native, not cross-compiler)
CC = /usr/bin/gcc
CFLAGS = -Wall -Wextra -std=c11 -g3 -O0
CFLAGS += -D_XOPEN_SOURCE=600 -D_GNU_SOURCE
CFLAGS += -pthread
CFLAGS += -I$(INCLUDE_DIR)
CFLAGS += -I$(OS_LINUX_DIR)/include
CFLAGS += -I$(MOCKS_DIR)
CFLAGS += -I$(COMMON_DIR)
CFLAGS += -I$(LINUX_DIR)
CFLAGS += -DIOHDLC_USE_POSIX
CFLAGS += $(CFLAGS_EXTRA)

# Linker flags
LDFLAGS = -pthread -lrt

# Source files
CORE_SRCS = \
	$(SRC_DIR)/ioHdlc_core.c \
	$(SRC_DIR)/ioHdlc.c \
	$(SRC_DIR)/ioHdlc_log.c \
	$(SRC_DIR)/ioHdlcll.c \
	$(SRC_DIR)/ioHdlcswdriver.c

OSAL_SRCS = \
	$(OS_LINUX_DIR)/src/ioHdlcosal.c \
	$(OS_LINUX_DIR)/src/ioHdlcfmempool.c \
	$(OS_LINUX_DIR)/src/ioHdlc_runner.c

MOCK_SRCS = \
	$(MOCKS_DIR)/mock_stream.c \
	$(MOCKS_DIR)/mock_stream_adapter.c

COMMON_SRCS = \
	$(COMMON_DIR)/test_helpers.c

# OS-agnostic test scenarios (shared across all platforms)
COMMON_TEST_SRCS = \
	$(COMMON_SCENARIOS_DIR)/test_frame_pool.c \
	$(COMMON_SCENARIOS_DIR)/test_basic_connection.c \
	$(COMMON_SCENARIOS_DIR)/test_basic_connection_twa.c \
	$(COMMON_SCENARIOS_DIR)/test_checkpoint_tws.c \
	$(COMMON_SCENARIOS_DIR)/test_checkpoint_twa.c

# Linux-specific test scenarios
LINUX_TEST_SRCS = \
	$(SCENARIOS_DIR)/test_mock_stream.c \
	$(SCENARIOS_DIR)/test_osal_bsem.c \
	$(SCENARIOS_DIR)/test_osal_events.c

# Test scenarios
TEST_MOCK_STREAM = test_mock_stream
TEST_BASIC_CONNECTION = test_basic_connection
TEST_BASIC_CONNECTION_TWA = test_basic_connection_twa
TEST_FRAME_POOL = test_frame_pool
TEST_CHECKPOINT_TWS = test_checkpoint_tws
TEST_CHECKPOINT_TWA = test_checkpoint_twa
TEST_OSAL_BSEM = test_osal_bsem
TEST_OSAL_EVENTS = test_osal_events

# Object files
CORE_OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(CORE_SRCS))
OSAL_OBJS = $(patsubst $(OS_LINUX_DIR)/src/%.c,$(OBJ_DIR)/osal_%.o,$(OSAL_SRCS))
MOCK_OBJS = $(patsubst $(MOCKS_DIR)/%.c,$(OBJ_DIR)/mock_%.o,$(MOCK_SRCS))
COMMON_OBJS = $(patsubst $(COMMON_DIR)/%.c,$(OBJ_DIR)/common_%.o,$(COMMON_SRCS))

ALL_OBJS = $(CORE_OBJS) $(OSAL_OBJS) $(MOCK_OBJS) $(COMMON_OBJS)

# Test binaries
TEST_BINS = $(BIN_DIR)/$(TEST_MOCK_STREAM)
TEST_BINS += $(BIN_DIR)/$(TEST_BASIC_CONNECTION)
TEST_BINS += $(BIN_DIR)/$(TEST_BASIC_CONNECTION_TWA)
TEST_BINS += $(BIN_DIR)/$(TEST_FRAME_POOL)
TEST_BINS += $(BIN_DIR)/$(TEST_CHECKPOINT_TWS)
TEST_BINS += $(BIN_DIR)/$(TEST_CHECKPOINT_TWA)
TEST_BINS += $(BIN_DIR)/$(TEST_OSAL_BSEM)
TEST_BINS += $(BIN_DIR)/$(TEST_OSAL_EVENTS)

# Targets
.PHONY: all clean test test-osal dirs

all: dirs $(TEST_BINS)

dirs:
	@mkdir -p $(OBJ_DIR) $(BIN_DIR)

# Compile core sources
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "CC $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Compile OSAL sources
$(OBJ_DIR)/osal_%.o: $(OS_LINUX_DIR)/src/%.c
	@echo "CC $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Compile mock sources
$(OBJ_DIR)/mock_%.o: $(MOCKS_DIR)/%.c
	@echo "CC $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Compile common sources
$(OBJ_DIR)/common_%.o: $(COMMON_DIR)/%.c
	@echo "CC $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Compile common test scenarios (OS-agnostic)
$(OBJ_DIR)/test_%.o: $(COMMON_SCENARIOS_DIR)/test_%.c
	@echo "CC $< (common)"
	@$(CC) $(CFLAGS) -c $< -o $@

# Compile Linux-specific test scenarios
$(OBJ_DIR)/test_%.o: $(SCENARIOS_DIR)/test_%.c
	@echo "CC $< (linux)"
	@$(CC) $(CFLAGS) -c $< -o $@

# Link test binaries
$(BIN_DIR)/$(TEST_MOCK_STREAM): $(OBJ_DIR)/test_mock_stream.o $(MOCK_OBJS) $(COMMON_OBJS)
	@echo "LD $@"
	@$(CC) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(TEST_BASIC_CONNECTION): $(OBJ_DIR)/test_basic_connection.o $(ALL_OBJS)
	@echo "LD $@"
	@$(CC) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(TEST_BASIC_CONNECTION_TWA): $(OBJ_DIR)/test_basic_connection_twa.o $(ALL_OBJS)
	@echo "LD $@"
	@$(CC) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(TEST_FRAME_POOL): $(OBJ_DIR)/test_frame_pool.o $(ALL_OBJS)
	@echo "LD $@"
	@$(CC) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(TEST_CHECKPOINT_TWS): $(OBJ_DIR)/test_checkpoint_tws.o $(ALL_OBJS)
	@echo "LD $@"
	@$(CC) $^ -o $@ $(LDFLAGS)
	
$(BIN_DIR)/$(TEST_CHECKPOINT_TWA): $(OBJ_DIR)/test_checkpoint_twa.o $(ALL_OBJS)
	@echo "LD $@"
	@$(CC) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(TEST_OSAL_BSEM): $(OBJ_DIR)/test_osal_bsem.o $(OBJ_DIR)/osal_ioHdlcosal.o $(COMMON_OBJS)
	@echo "LD $@"
	@$(CC) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(TEST_OSAL_EVENTS): $(OBJ_DIR)/test_osal_events.o $(OBJ_DIR)/osal_ioHdlcosal.o $(COMMON_OBJS)
	@echo "LD $@"
	@$(CC) $^ -o $@ $(LDFLAGS)

# Run tests
test: all
	@echo ""
	@echo "════════════════════════════════════════════════════════"
	@echo "  Running Test Suite"
	@echo "════════════════════════════════════════════════════════"
	@echo ""
	@for test in $(TEST_BINS); do \
		$$test || exit 1; \
	done
	@echo ""
	@echo "════════════════════════════════════════════════════════"
	@echo "  All Tests Completed Successfully"
	@echo "════════════════════════════════════════════════════════"
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

# Help
help:
	@echo "ioHdlc Linux Test Suite"
	@echo ""
	@echo "Targets:"
	@echo "  all       - Build all test binaries"
	@echo "  test      - Build and run all tests"
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
	@echo "Logging levels (IOHDLC_LOG_LEVEL):"
	@echo "  0 - OFF (default, zero overhead)"
	@echo "  1 - FRAMES (frame headers only)"
	@echo "  2 - DATA (headers + partial payload)"
	@echo "  3 - FULL (headers + complete hex dump)"
