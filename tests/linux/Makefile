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

# Linker flags
LDFLAGS = -pthread -lrt

# Source files
CORE_SRCS = \
	$(SRC_DIR)/ioHdlc_core.c \
	$(SRC_DIR)/ioHdlc.c \
	$(SRC_DIR)/ioHdlcll.c \
	$(SRC_DIR)/ioHdlcstream.c \
	$(SRC_DIR)/ioHdlcstream_driver.c

OSAL_SRCS = \
	$(OS_LINUX_DIR)/src/ioHdlcosal.c \
	$(OS_LINUX_DIR)/src/ioHdlcfmempool.c

MOCK_SRCS = \
	$(MOCKS_DIR)/mock_stream.c

COMMON_SRCS = \
	$(COMMON_DIR)/test_helpers.c

# OS-agnostic test scenarios (shared across all platforms)
COMMON_TEST_SRCS = \
	$(COMMON_SCENARIOS_DIR)/test_frame_pool.c \
	$(COMMON_SCENARIOS_DIR)/test_basic_connection.c

# Linux-specific test scenarios
LINUX_TEST_SRCS = \
	$(SCENARIOS_DIR)/test_mock_stream.c

# Test scenarios
TEST_MOCK_STREAM = test_mock_stream
TEST_BASIC_CONNECTION = test_basic_connection
TEST_FRAME_POOL = test_frame_pool

# Object files
CORE_OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(CORE_SRCS))
OSAL_OBJS = $(patsubst $(OS_LINUX_DIR)/src/%.c,$(OBJ_DIR)/osal_%.o,$(OSAL_SRCS))
MOCK_OBJS = $(patsubst $(MOCKS_DIR)/%.c,$(OBJ_DIR)/mock_%.o,$(MOCK_SRCS))
COMMON_OBJS = $(patsubst $(COMMON_DIR)/%.c,$(OBJ_DIR)/common_%.o,$(COMMON_SRCS))

ALL_OBJS = $(CORE_OBJS) $(OSAL_OBJS) $(MOCK_OBJS) $(COMMON_OBJS)

# Test binaries
TEST_BINS = $(BIN_DIR)/$(TEST_MOCK_STREAM)
TEST_BINS += $(BIN_DIR)/$(TEST_BASIC_CONNECTION)
TEST_BINS += $(BIN_DIR)/$(TEST_FRAME_POOL)

# Targets
.PHONY: all clean test dirs

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

$(BIN_DIR)/$(TEST_FRAME_POOL): $(OBJ_DIR)/test_frame_pool.o $(OSAL_OBJS) $(COMMON_OBJS)
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

# Clean
clean:
	@echo "Cleaning build directory..."
	@rm -rf $(BUILD_DIR)

# Help
help:
	@echo "ioHdlc Linux Test Suite"
	@echo ""
	@echo "Targets:"
	@echo "  all     - Build all test binaries"
	@echo "  test    - Build and run all tests"
	@echo "  clean   - Remove build artifacts"
	@echo "  help    - Show this message"
	@echo ""
	@echo "Examples:"
	@echo "  make test              # Run all tests"
	@echo "  make clean && make     # Rebuild from scratch"
