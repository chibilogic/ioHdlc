# Test Architecture - OS-Agnostic Design

## Philosophy

The ioHdlc test architecture follows the principle of **maximum portability**: tests that use only abstract interfaces are shared across all platforms, while only OS-specific tests remain separate.

## Test Structure

### OS-Agnostic Tests

**Source:** `tests/common/scenarios/`

These tests use **only abstract interfaces** defined in the ioHdlc core:

1. **test_basic.c**
   - Interface: `ioHdlc` core (stations, peers, handshake)
   - Tests: Station/peer creation, SNRM/UA, timeout
   - Portability: ✅ Works on any platform

2. **test_window_management.c**
   - Interface: `ioHdlc` core (sequencing, window control)
   - Tests: Window size limits, window sliding, acknowledgment
   - Portability: ✅ Works on Linux, ChibiOS, generic RTOS

3. **test_checkpoint_tws.c**
   - Interface: `ioHdlc` core with mock stream error injection
   - Tests: Single frame loss, multiple frame loss, checkpoint retransmission
   - Portability: ✅ Works on all platforms

**Characteristics:**
- No `#include` of OS-specific headers
- No direct calls to pthread, malloc, etc.
- Only VMT-based interfaces and abstract macros
- Same source code compiled for all platforms

### Platform-Specific Tests

#### Linux

**Source:** `tests/linux/`

1. **mock_stream.c**
   - Tests mock stream implementation with pthread
   - Uses `mock_stream.h` (POSIX-specific)
   - Validates circular buffers, loopback, peer connection

#### ChibiOS

**Source:** `tests/chibios/`

- ✅ Mock stream with ChibiOS threads and virtual timers
- Tests with simulated UART
- Integration testing on embedded target

## Build System

### Linux Makefile

```makefile
# Common tests (OS-agnostic)
COMMON_TEST_SRCS = \
    $(COMMON_DIR)/scenarios/test_basic.c \
    $(COMMON_DIR)/scenarios/test_window_management.c \
    $(COMMON_DIR)/scenarios/test_checkpoint_tws.c

# Linux-specific mocks
LINUX_MOCKS = \
    $(MOCKS_DIR)/mock_stream.c \
    $(MOCKS_DIR)/tssi_stubs.c
```

### ChibiOS Makefile

```makefile
# Same common tests
COMMON_TEST_SRCS = \
    $(COMMON_DIR)/scenarios/test_basic.c \
    $(COMMON_DIR)/scenarios/test_window_management.c \
    $(COMMON_DIR)/scenarios/test_checkpoint_tws.c

# ChibiOS-specific mocks
CHIBIOS_MOCKS = \
    $(MOCKS_DIR)/mock_stream_chibios.c \
    $(MOCKS_DIR)/tssi_stubs.c
```

## Advantages

### 1. Maximum Reuse
- Write the test **once**
- Run it on **all platforms**
- Maintain **a single copy**

### 2. Portability Guarantee
If `test_basic.c` compiles and passes on Linux, we know that:
- The `ioHdlc` core interface is correctly implemented
- The macros work correctly
- The behavior is consistent cross-platform

### 3. Maintainability
- Bug fix: modify in one place
- New tests: automatically available for all platforms
- Refactoring: reduced impact

### 4. Uniform Coverage
Each platform gets:
- ✅ Complete basic tests (10+ assertions)
- ✅ Window management tests (11+ assertions)
- ✅ Checkpoint retransmission tests (45+ assertions)
- ✅ Platform-specific tests

## Guidelines for New Tests

### Test should be OS-agnostic if:
- ✅ Uses only interfaces defined in `include/*.h`
- ✅ Uses only public macros (`hdlcXxx`, `iohdlc_xxx`)
- ✅ Makes no assumptions about threading model
- ✅ Does not use OS-specific types (`pthread_t`, `Thread*`)

→ **Put in `tests/common/scenarios/`**

### Test should be platform-specific if:
- ❌ Uses OS-specific APIs (pthread, chThdCreate)
- ❌ Tests OSAL implementation
- ❌ Tests specific mocks/drivers
- ❌ Requires specific hardware

→ **Put in `tests/<platform>/mocks/` or `tests/<platform>/`**

## Future Examples

### OS-Agnostic (common)
```c
// test_rej_recovery.c - Common
#include "ioHdlc.h"
#include "ioHdlcframe.h"

int test_rej_sequence(void) {
    iohdlc_frame_t *frame = hdlcTakeFrame(pool);
    // ... test REJ frame sequencing ...
    hdlcReleaseFrame(pool, frame);
}
```

### Platform-Specific (linux)
```c
// test_pthread_safety.c - Linux
#include "ioHdlcosal.h"
#include <pthread.h>

int test_concurrent_allocation(void) {
    pthread_t threads[10];
    // ... test thread-safety ...
}
```

## Current Metrics

**OS-Agnostic Tests:**
- test_basic.c: 10+ assertions, 2 scenarios
- test_window_management.c: 11+ assertions, 2 scenarios
- test_checkpoint_tws.c: 45+ assertions, 3 scenarios
- **Total: 66+ portable assertions**

**Platform-Specific Tests:**
- Linux mock_stream: Error injection, loopback validation
- ChibiOS mock_stream: ChibiOS threads, virtual timers

**Coverage:**
- Basic functionality: 100% (init, deinit, handshake)
- Window management: 80% (size limits, sliding, acknowledgment)
- Checkpoint retransmission: 100% (single/multiple frame loss)
- Error recovery: 30% (checkpoint implemented, REJ pending)
- Flow control: 0% (RNR/RR not yet tested)

## Next Steps

1. Implement test_rej_recovery.c (OS-agnostic)
2. Implement test_flow_control.c (OS-agnostic - RNR/RR)
3. Implement test_disconnect.c (OS-agnostic - DISC/UA)
4. Add CI to run tests on multiple platforms
5. Add hardware tests for ChibiOS (physical UART loopback)
