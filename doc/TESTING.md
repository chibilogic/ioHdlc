# Testing Guide

## Overview

This document explains how to **write, debug, and extend** tests for the ioHdlc protocol stack.

- **What tests exist and how to run them** -- see [tests/README.md](../tests/README.md)
- **Why the test system is designed this way** -- see [TEST_ARCHITECTURE.md](TEST_ARCHITECTURE.md)

## Test Framework API

### Assertion Macros

Defined in `tests/common/test_helpers.h`. All macros print file and line on failure.

| Macro | Description |
|-------|-------------|
| `TEST_ASSERT(condition, msg)` | Basic boolean check |
| `TEST_ASSERT_EQ(expected, actual, msg)` | Equality (prints both values on failure) |
| `TEST_ASSERT_NEQ(expected, actual, msg)` | Inequality |
| `TEST_ASSERT_RANGE(min, max, actual, msg)` | Value within range |
| `TEST_ASSERT_NULL(ptr, msg)` | Pointer is NULL |
| `TEST_ASSERT_NOT_NULL(ptr, msg)` | Pointer is not NULL |

`TEST_ASSERT_EQUAL` is an alias for `TEST_ASSERT_EQ`.

On failure, non-GOTO macros `return 1` immediately. On success, they increment `test_successes`.

### GOTO Variants

Every assertion macro has a `_GOTO` variant (`TEST_ASSERT_GOTO`, `TEST_ASSERT_EQ_GOTO`, etc.) that jumps to a cleanup label instead of returning. Use these when the test allocates resources that must be freed:

```c
int test_my_scenario(const test_adapter_t *adapter) {
  int test_result = 0;

  /* setup ... */
  ioHdlcRunnerStart(&station);

  TEST_ASSERT_EQ_GOTO(96, bytes_received, "Wrong byte count");
  TEST_ASSERT_GOTO(bytes_received == bytes_sent, "Data mismatch");

test_cleanup:
  ioHdlcStationDeinit(&station);
  return test_result;
}
```

### Runner Macros

| Macro | Description |
|-------|-------------|
| `RUN_TEST(fn)` | Run a test taking no arguments |
| `RUN_TEST_ADAPTER(fn, adapter)` | Run a test taking a `const test_adapter_t *` |
| `TEST_SUITE_START(name)` | Print suite header |
| `TEST_SUITE_END()` | Print suite summary (pass/fail counts) |
| `TEST_SUMMARY()` | Print overall summary |

### Utility Functions

| Function / Macro | Description |
|------------------|-------------|
| `test_hexdump(label, data, len)` | Print hex dump of a byte buffer |
| `test_mem_equal(a, b, len)` | Compare byte arrays (returns `bool`) |
| `U64_FMT` / `U64_ARGS(val)` | Portable `uint64_t` decimal printing |
| `U64_KB` / `U64_KB_ARGS(val)` | Print `uint64_t` in KB |
| `test_printf(...)` | Platform-aware printf (maps to `IOHDLC_OSAL_PRINTF`) |

## Writing a New OS-Agnostic Test

### Step 1: Create the scenario

Create `tests/common/scenarios/test_<name>.c`.

Test functions return `int` (0 = pass) or `bool` (true = pass). Functions that need stream access take a `const test_adapter_t *adapter` parameter; standalone tests (like frame pool) take no arguments.

```c
#include "../test_helpers.h"
#include "../test_scenarios.h"
#include "../adapters/adapter_interface.h"
#include "../test_arenas.h"

int test_my_feature(const test_adapter_t *adapter) {
  int test_result = 0;

  /* Get stream ports from the adapter */
  ioHdlcStreamPort port_a = adapter->get_port_a();
  ioHdlcStreamPort port_b = adapter->get_port_b();

  /* Configure and start stations using port_a / port_b ... */

  TEST_ASSERT_EQ_GOTO(expected, actual, "Description");

test_cleanup:
  /* Teardown stations ... */
  return test_result;
}
```

### Step 2: Declare in test_scenarios.h

Add the prototype to `tests/common/test_scenarios.h`:

```c
int test_my_feature(const test_adapter_t *adapter);
```

### Step 3: Create the platform runner

Create `tests/linux/test_runner_<name>.c`:

```c
#include "../common/test_helpers.h"
#include "../common/test_scenarios.h"
#include "adapter_mock.h"

int main(void) {
  test_printf("\n");
  test_printf("═══════════════════════════════════════════════\n");
  test_printf("  ioHdlc Test Suite - My Feature\n");
  test_printf("═══════════════════════════════════════════════\n\n");

  mock_adapter.init();
  RUN_TEST_ADAPTER(test_my_feature, &mock_adapter);
  mock_adapter.deinit();

  TEST_SUMMARY();
  return (failed_count == 0) ? 0 : 1;
}
```

For tests that don't need an adapter (e.g. frame pool), use `RUN_TEST(fn)` directly.

### Step 4: Add to the Makefile

In `tests/linux/Makefile`:

```makefile
# Add variable
TEST_MY_FEATURE = test_my_feature

# Add to AUTO_TEST_BINS (or MANUAL_TEST_BINS for parametrized tests)
AUTO_TEST_BINS += $(BIN_DIR)/$(TEST_MY_FEATURE)

# Add link rule
$(BIN_DIR)/$(TEST_MY_FEATURE): $(OBJ_DIR)/test_runner_my_feature.o $(OBJ_DIR)/test_my_feature.o $(ALL_OBJS)
	@echo "LD $@"
	@$(CC) $^ -o $@ $(LDFLAGS)
```

Build and run:

```bash
make -C tests/linux
./tests/linux/build/bin/test_my_feature
```

## Writing a Platform-Specific Test

Platform-specific tests live in `tests/linux/scenarios/` (or `tests/chibios/`). They are self-contained with their own `main()` and do not use the adapter abstraction.

Use this for tests that exercise OS primitives directly -- e.g. `test_osal_bsem.c` tests the POSIX binary semaphore implementation using `pthread` calls.

In the Makefile, these link against minimal dependencies (OSAL + common helpers), not the full `ALL_OBJS`.

## Error Injection

### Error Filter Callback

The mock stream supports selective frame corruption through a filter callback:

```c
typedef bool (*mock_stream_error_filter_t)(uint32_t write_count,
                                            const uint8_t *data,
                                            size_t size,
                                            void *userdata);
```

- `write_count`: monotonic counter of all writes (U, S, I frames).
- `data`: raw frame bytes (flag, address, length, control, payload, FCS).
- Return `true` to corrupt the frame (FCS bits flipped), `false` to pass through.

### Configuring Error Injection

```c
mock_stream_config_t config = {
  .loopback       = false,
  .inject_errors  = true,
  .error_rate     = 0,                 /* Unused when filter is set */
  .error_filter   = my_filter,
  .error_userdata = NULL
};
```

When `inject_errors` is `true` and `error_filter` is `NULL`, the mock stream corrupts frames randomly at `error_rate` percent.

For detailed error injection examples (dropping specific I-frames by N(S), multi-frame loss), see [tests/README.md](../tests/README.md).

## Parametrized Testing

The `test_exchange` tool is a configurable HDLC stress test supporting multiple modes, traffic patterns, error injection, and long-running operation. See [Exchange Test Tool](TEST_EXCHANGE.md) for full documentation.

## Debugging Tests

### Logging Levels

Defined in `include/ioHdlc_log.h`. Compile-time selection via `-DIOHDLC_LOG_LEVEL=N`:

| Level | Name | Output |
|-------|------|--------|
| 0 | OFF | No logging (default, zero overhead) |
| 1 | FRAMES | Frame headers only |
| 2 | DATA | Headers + partial payload |
| 3 | FULL | Headers + complete hex dump |

```bash
make -C tests/linux clean
make -C tests/linux CFLAGS_EXTRA="-DIOHDLC_LOG_LEVEL=2"
./tests/linux/build/bin/test_checkpoint_tws
```

### Station State Dump

`test_dump_station_state()` (from `test_framework.h`) prints station and peer variables (V(S), V(R), V(A), state, window) for debugging protocol issues.

### GDB

```bash
gdb ./tests/linux/build/bin/test_basic_connection
(gdb) run
(gdb) bt
```

### Valgrind

```bash
valgrind --leak-check=full ./tests/linux/build/bin/test_basic_connection
```

### Thread Sanitizer

```bash
make -C tests/linux clean
make -C tests/linux CFLAGS_EXTRA="-fsanitize=thread"
./tests/linux/build/bin/test_basic_connection
```

For the full list of debugging techniques and tools, see [tests/README.md](../tests/README.md).

## Best Practices

**Do:**

- Write clear test names: `test_<category>_<specific_case>` (e.g. `test_checkpoint_retransmit_on_timeout`).
- Use descriptive assertion messages with context values.
- Test one thing per test function.
- Use `_GOTO` variants when cleanup is needed (stations, runners).
- Call `adapter.init()` / `adapter.deinit()` per test to avoid cross-test state leakage.
- Use realistic timeouts to avoid flaky tests.

**Don't:**

- Use hardcoded delays -- rely on events and OSAL synchronization.
- Share mutable state between tests.
- Test implementation details -- test observable behavior.
- Use magic numbers -- define named constants.
- Skip cleanup -- resource leaks accumulate across the suite.

## References

- [Test Suite README](../tests/README.md) -- what tests exist, how to build and run, implemented/planned checklists
- [Test Architecture](TEST_ARCHITECTURE.md) -- design rationale, portability, adapter abstraction
