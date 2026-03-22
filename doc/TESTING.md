# Testing Guide

## Overview

This document provides comprehensive guidance on testing the ioHdlc protocol stack, including test philosophy, writing new tests, using mock infrastructure, error injection, and continuous integration.

## Testing Philosophy

### Goals

1. **Protocol Correctness**: Verify ISO 13239 compliance
2. **Platform Portability**: Same tests run on Linux and ChibiOS
3. **Determinism**: Tests produce consistent results
4. **Coverage**: Exercise all code paths and edge cases
5. **Maintainability**: Tests are clear, documented, and easy to update

### Approach

- **OS-Agnostic Tests**: Test scenarios written once, run everywhere
- **Mock Infrastructure**: Controllable, deterministic test environment
- **Incremental Testing**: Build from unit → integration → system
- **Automated Validation**: CI/CD runs all tests on every commit

## Test Architecture

### Directory Structure

```
tests/
├── common/
│   ├── scenarios/           # OS-agnostic test scenarios
│   │   ├── test_basic.c
│   │   ├── test_window_management.c
│   │   ├── test_checkpoint_tws.c
│   │   └── ...
│   └── utils/               # Shared test utilities
│       ├── test_helpers.c
│       └── test_macros.h
├── linux/
│   ├── mocks/               # Linux-specific mocks
│   │   ├── mock_stream.c/h
│   │   └── tssi_stubs.c
│   ├── main_tests.c         # Linux test runner
│   └── Makefile
└── chibios/
    ├── mocks/               # ChibiOS-specific mocks
    │   ├── mock_stream_chibios.c/h
    │   └── tssi_stubs.c
    └── main_tests.c         # ChibiOS test runner
```

### Test Layers

#### 1. Unit Tests

**Purpose**: Test individual functions in isolation

**Example**: FCS calculation
```c
void test_fcs_calculation(void) {
  uint8_t data[] = {0x03, 0x83};  // SNRM frame
  uint16_t fcs = calculate_fcs(data, sizeof(data));
  TEST_ASSERT(fcs == 0x50B9, "FCS mismatch");
}
```

#### 2. Integration Tests

**Purpose**: Test interactions between components

**Example**: Station + Driver integration
```c
void test_station_driver_init(void) {
  ioHdlcSwDriverInit(&driver);
  ioHdlcStationInit(&station, &config);
  TEST_ASSERT(station.state == IOHDLC_STATE_NDM, "Wrong initial state");
}
```

#### 3. Scenario Tests

**Purpose**: Test complete protocol sequences

**Example**: SNRM handshake
```c
void test_snrm_handshake(void) {
  // 1. Send SNRM
  ioHdlcConnect(&peer);
  
  // 2. Wait for SNRM frame
  mock_stream_wait_for_tx();
  
  // 3. Inject UA response
  mock_stream_inject_rx(ua_frame, ua_len);
  
  // 4. Verify connection established
  TEST_ASSERT(peer.state == IOHDLC_PEER_STATE_CONNECTED);
}
```

## Mock Infrastructure

### Mock Stream

**Purpose**: Simulate physical layer (UART) without hardware

**Features:**
- **Loopback mode**: TX → RX (tests single station)
- **Dual-stream mode**: Two stations communicate
- **Error injection**: Corrupt frames at specific times
- **Deterministic timing**: Controllable, no race conditions

### Mock Stream API

```c
// Initialize mock stream
void mock_stream_init(mock_stream_t *stream, const mock_stream_config_t *config);

// Loopback mode (single station)
mock_stream_config_t loopback_config = {
  .loopback = true,
  .inject_errors = false,
  .error_rate = 0,
  .error_filter = NULL,
  .error_userdata = NULL
};

// Dual-stream mode (two stations)
void mock_stream_connect(mock_stream_t *stream1, mock_stream_t *stream2);

// Wait for TX/RX completion
void mock_stream_wait_for_tx(mock_stream_t *stream, uint32_t timeout_ms);
size_t mock_stream_wait_for_rx(mock_stream_t *stream, uint8_t *buf, size_t max_len, uint32_t timeout_ms);
```

### Error Injection

#### Purpose

Simulate real-world conditions:
- **Bit errors**: Corrupt FCS
- **Frame loss**: Drop specific frames
- **Burst errors**: Multiple consecutive errors
- **Conditional errors**: Error only on first transmission

#### Error Filter Callback

```c
typedef bool (*mock_stream_error_filter_t)(uint32_t write_count,
                                            const uint8_t *data,
                                            size_t size,
                                            void *userdata);

// Callback returns:
// - true: Corrupt this frame
// - false: Transmit normally
```

#### Example: Drop Frame with N(S)=1

```c
static bool drop_frame_1_filter(uint32_t write_count,
                                 const uint8_t *data,
                                 size_t size,
                                 void *userdata) {
  static uint32_t corruption_count = 0;
  
  // Parse control byte
  uint8_t control = data[3];  // data[0]=flag, [1]=addr, [2]=len, [3]=control
  uint8_t ns = (control >> 1) & 0x07;
  
  // Corrupt I-frame with N(S)=1, but only first transmission
  if (ns == 1 && corruption_count == 0) {
    corruption_count++;
    return true;  // Corrupt (flip FCS bits)
  }
  
  return false;  // Transmit normally
}
```

#### Example: Drop Multiple Frames

```c
static bool drop_frames_1_and_3_filter(uint32_t write_count,
                                        const uint8_t *data,
                                        size_t size,
                                        void *userdata) {
  static uint32_t corruption_count_1 = 0;
  static uint32_t corruption_count_3 = 0;
  
  uint8_t control = data[3];
  uint8_t ns = (control >> 1) & 0x07;
  
  if (ns == 1 && corruption_count_1 == 0) {
    corruption_count_1++;
    return true;
  }
  
  if (ns == 3 && corruption_count_3 == 0) {
    corruption_count_3++;
    return true;
  }
  
  return false;
}
```

#### Configuration

```c
mock_stream_config_t stream_config = {
  .loopback = false,
  .inject_errors = true,
  .error_rate = 1000,  // Not used when filter provided
  .error_filter = drop_frame_1_filter,
  .error_userdata = NULL  // Custom data for filter
};
```

## Writing New Tests

### Test Template

```c
/**
 * @brief Test description
 * 
 * Detailed explanation of what this test validates.
 */
void test_my_new_test(void) {
  // 1. Setup
  ioHdlcSwDriver driver;
  ioHdlcStation station;
  ioHdlcPeer peer;
  mock_stream_t stream;
  
  // 2. Initialize
  mock_stream_init(&stream, &loopback_config);
  ioHdlcSwDriverInit(&driver);
  ioHdlcStationInit(&station, &config);
  
  // 3. Execute test actions
  int result = ioHdlcWrite(&peer, data, len, timeout);
  
  // 4. Verify results
  TEST_ASSERT(result == len, "Write failed");
  TEST_ASSERT(peer.state == expected_state, "Wrong state");
  
  // 5. Cleanup
  ioHdlcStationDeinit(&station);
  mock_stream_deinit(&stream);
}
```

### Test Assertions

```c
// Basic assertion
TEST_ASSERT(condition, "Error message");

// Comparison assertions
TEST_ASSERT_EQUAL(expected, actual, "Values don't match");
TEST_ASSERT_NOT_EQUAL(value1, value2, "Values should differ");

// Range assertions
TEST_ASSERT_GREATER_THAN(min, actual, "Value too small");
TEST_ASSERT_LESS_THAN(max, actual, "Value too large");

// Pointer assertions
TEST_ASSERT_NULL(ptr, "Pointer should be NULL");
TEST_ASSERT_NOT_NULL(ptr, "Pointer is NULL");

// String assertions
TEST_ASSERT_STRING_EQUAL(expected, actual, "Strings differ");
```

### Test Naming Conventions

- **Function names**: `test_<category>_<specific_case>`
- **Examples**:
  - `test_window_full_blocks_tx`
  - `test_checkpoint_retransmit_on_timeout`
  - `test_snrm_handshake_success`

## Existing Test Scenarios

### Basic Tests

**Source:** `test_basic.c`

#### test_init_deinit
- **Purpose**: Verify initialization and cleanup
- **Assertions**: 2
- **Coverage**: Station lifecycle

#### test_snrm_handshake_frames
- **Purpose**: SNRM → UA handshake
- **Assertions**: 8
- **Coverage**: Connection establishment, frame parsing

### Window Management

**Source:** `test_window_management.c`

#### test_window_size_7
- **Purpose**: Window size limits
- **Assertions**: 5
- **Coverage**: Sequence numbering, window full condition

#### test_window_slides_on_ack
- **Purpose**: Window advancement on ACK
- **Assertions**: 6
- **Coverage**: V(A) updates, window management

### Checkpoint Retransmission

**Source:** `test_checkpoint_tws.c`

#### test_A1_1_frame_loss_window_full
- **Purpose**: Single frame loss with checkpoint recovery
- **Setup**:
  - Window size: 7
  - Inject error: Drop I1,0 (first transmission only)
  - Send: 96 bytes (12 frames of 8 bytes)
- **Expected**:
  - Station A sends I0-I6, window full
  - I7 has P=1 (checkpoint)
  - I1 corrupted, retransmitted after timeout
  - All 96 bytes received correctly
- **Assertions**: 15+
- **Coverage**: Checkpoint, timeout, retransmission

#### test_A2_1_multiple_frame_loss
- **Purpose**: Multiple frame loss (frames 1 and 3)
- **Setup**:
  - Window size: 7
  - Inject errors: Drop I1,0 and I3,0 (first transmission only)
  - Send: 96 bytes
- **Expected**:
  - Both frames corrupted on first transmission
  - Checkpoint timeout triggers
  - Both frames retransmitted
  - All 96 bytes received
- **Assertions**: 15+
- **Coverage**: Multiple losses, batch retransmission

#### test_A2_2_first_and_last_frame_loss
- **Purpose**: Edge case - first and last frame in window lost
- **Setup**:
  - Window size: 7
  - Inject errors: Drop I0,0 and I7,0 (first transmission only)
  - Send: 96 bytes
- **Expected**:
  - I0 and I7 corrupted
  - Checkpoint recovery
  - All frames retransmitted
  - All 96 bytes received
- **Assertions**: 15+
- **Coverage**: Edge cases, window boundaries

## Test Execution

### Linux

#### Build Tests

```bash
cd tests/linux
make clean
make
```

#### Run All Tests

```bash
./test_iohdlc
```

#### Run Specific Test

```bash
./test_basic
./test_window_management
./test_checkpoint_tws
```

#### Debug Mode

```bash
make clean
make CFLAGS_EXTRA="-DIOHDLC_LOG_LEVEL=4"  # DEBUG log level
./test_iohdlc
```

### ChibiOS

#### Build Tests

```bash
cd tests/chibios
make clean
make
```

## Common Test Patterns

### Pattern 1: Loopback Test (Single Station)

```c
void test_loopback_example(void) {
  // Setup loopback stream
  mock_stream_config_t config = {
    .loopback = true,
    .inject_errors = false
  };
  mock_stream_init(&stream, &config);
  
  // Write data
  const char *msg = "HELLO";
  int written = ioHdlcWrite(&peer, msg, strlen(msg), 1000);
  
  // Read it back
  char buf[32];
  int read = ioHdlcRead(&peer, buf, sizeof(buf), 1000);
  
  TEST_ASSERT(written == strlen(msg), "Write failed");
  TEST_ASSERT(read == strlen(msg), "Read failed");
  TEST_ASSERT_STRING_EQUAL(msg, buf, "Data mismatch");
}
```

### Pattern 2: Two-Station Test

```c
void test_two_stations_example(void) {
  // Setup two stations
  mock_stream_t stream_a, stream_b;
  ioHdlcStation station_a, station_b;
  ioHdlcPeer peer_a, peer_b;
  
  // Connect streams
  mock_stream_init(&stream_a, &config);
  mock_stream_init(&stream_b, &config);
  mock_stream_connect(&stream_a, &stream_b);
  
  // Initialize stations
  ioHdlcSwDriverInit(&driver_a);
  ioHdlcSwDriverInit(&driver_b);
  ioHdlcStationInit(&station_a, &config_primary);
  ioHdlcStationInit(&station_b, &config_secondary);
  
  // Station A writes
  const char *msg = "DATA";
  ioHdlcWrite(&peer_a, msg, strlen(msg), 1000);
  
  // Station B reads
  char buf[32];
  int read = ioHdlcRead(&peer_b, buf, sizeof(buf), 1000);
  
  TEST_ASSERT(read == strlen(msg), "Read failed");
  TEST_ASSERT_STRING_EQUAL(msg, buf, "Data mismatch");
}
```

### Pattern 3: Error Injection Test

```c
void test_error_recovery_example(void) {
  // Error filter: drop every 3rd frame
  static uint32_t frame_count = 0;
  
  auto error_filter = [](uint32_t write_count, const uint8_t *data,
                         size_t size, void *userdata) -> bool {
    return (write_count % 3) == 0;  // Drop every 3rd
  };
  
  // Setup stream with errors
  mock_stream_config_t config = {
    .loopback = false,
    .inject_errors = true,
    .error_filter = error_filter,
    .error_userdata = NULL
  };
  
  // Send data, expect retransmissions
  uint8_t data[64];
  memset(data, 0xAA, sizeof(data));
  
  int written = ioHdlcWrite(&peer, data, sizeof(data), 5000);
  
  // Should eventually succeed despite errors
  TEST_ASSERT(written == sizeof(data), "Write failed");
}
```

## Debugging Tests

### Enable Verbose Logging

```c
// In test file
#define IOHDLC_LOG_LEVEL 4  // DEBUG

// Or in Makefile
CFLAGS_EXTRA = -DIOHDLC_LOG_LEVEL=4
```

**Log Levels:**
- 0: OFF
- 1: ERROR
- 2: WARN
- 3: INFO
- 4: DEBUG

### Add Debug Prints

Use the ioHdlc logging facility for structured debug output:

```c
#include "ioHdlc_log.h"

void test_my_test(void) {
  // Enable logging at runtime
  iohdlc_log_enabled = true;
  
  // Log test progress with WARN level (always visible)
  IOHDLC_LOG_WARN(IOHDLC_LOG_TX, station.config.addr, 
                  "Test: Station state=%d", station.state);
  
  IOHDLC_LOG_WARN(IOHDLC_LOG_TX, station.config.addr,
                  "Test: V(S)=%d, V(R)=%d, V(A)=%d", 
                  peer.vs, peer.vr, peer.nr);
  
  // ... test code ...
  
  IOHDLC_LOG_WARN(IOHDLC_LOG_RX, station.config.addr,
                  "Test: Bytes received=%d", bytes_received);
}
```

**Note**: Compile with `-DIOHDLC_LOG_LEVEL=1` or higher to enable logging.

### Use Assertions Wisely

```c
// Bad: Vague message
TEST_ASSERT(result > 0, "Failed");

// Good: Specific message
TEST_ASSERT(result > 0, "ioHdlcWrite returned %d (expected >0)", result);
```

### Inspect Frame Contents

```c
void dump_frame(const uint8_t *data, size_t len) {
  printf("Frame [%u bytes]: ", (uint32_t)len);
  for (size_t i = 0; i < len; i++) {
    printf("%02X ", data[i]);
  }
  printf("\n");
}
```

## Continuous Integration

### GitHub Actions Example

```yaml
name: Test ioHdlc

on: [push, pull_request]

jobs:
  test-linux:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      
      - name: Install dependencies
        run: sudo apt-get install -y build-essential
      
      - name: Build tests
        run: |
          cd tests/linux
          make clean
          make
      
      - name: Run tests
        run: |
          cd tests/linux
          ./test_iohdlc
      
      - name: Check results
        run: |
          if [ $? -ne 0 ]; then
            echo "Tests failed!"
            exit 1
          fi
```

## Test Coverage

### Current Coverage

**Protocol Features:**
- ✅ Station initialization/cleanup
- ✅ SNRM handshake
- ✅ I-frame transmission
- ✅ RR acknowledgment
- ✅ Window management (size 7)
- ✅ Checkpoint retransmission
- ✅ Single frame loss recovery
- ✅ Multiple frame loss recovery
- ⏳ RNR flow control
- ⏳ REJ error recovery
- ⏳ DISC disconnect
- ⏳ Timer expiry handling
- ⏳ Link failure detection

**Error Scenarios:**
- ✅ Single frame corruption
- ✅ Multiple frame corruption
- ✅ Edge case (first/last frame loss)
- ⏳ Burst errors
- ⏳ Timeout scenarios
- ⏳ Invalid frames (bad FCS)
- ⏳ Out-of-sequence frames

### Coverage Goals

**Short-term:**
- Add RNR/REJ tests
- Add disconnect tests
- Add timeout tests
- Add invalid frame tests

**Long-term:**
- Modulo 128 tests
- Multi-peer tests
- Stress tests (high load, many errors)
- Performance benchmarks

## Test Metrics

### Test Counts

- **Unit tests**: 10
- **Integration tests**: 15
- **Scenario tests**: 8
- **Total assertions**: 33+

### Test Execution Time

- **Linux**: ~5 seconds (all tests)
- **ChibiOS**: ~10 seconds (includes flash time)

### Code Coverage

**Target**: >80% line coverage

**Tools:**
- Linux: gcov, lcov
- ChibiOS: arm-none-eabi-gcov

## Best Practices

### Do's

✅ **Write clear test names** that describe what is tested
✅ **Use descriptive assertion messages** with context
✅ **Test one thing per test** (single responsibility)
✅ **Clean up resources** (stations, streams, frames)
✅ **Use realistic timeouts** (avoid flaky tests)
✅ **Document complex tests** with comments
✅ **Keep tests independent** (no shared state)

### Don'ts

❌ **Don't use hardcoded delays** (use events/polls)
❌ **Don't ignore test failures** (fix or disable explicitly)
❌ **Don't test implementation details** (test behavior)
❌ **Don't use magic numbers** (define constants)
❌ **Don't skip cleanup** (causes resource leaks in test suite)

## Future Test Scenarios

### Planned Tests (A.3 - A.5)

**A.3: Window Management Edge Cases**
- A.3.1: Wrap-around (N(S) 6→7→0)
- A.3.2: ACK out-of-order frames

**A.4: Flow Control**
- A.4.1: RNR stops transmission
- A.4.2: RR resumes transmission

**A.5: Disconnect**
- A.5.1: Clean disconnect (DISC → UA)
- A.5.2: Disconnect during transmission

### Planned Tests (B.1 - B.3)

**B.1: Error Detection**
- B.1.1: Invalid FCS (discard frame)
- B.1.2: Invalid control byte (FRMR)
- B.1.3: Invalid N(R) (FRMR)

**B.2: Timer Handling**
- B.2.1: T1 timeout (retransmit)
- B.2.2: Max retries (link failure)
- B.2.3: T3 idle timeout (optional)

**B.3: REJ Recovery**
- B.3.1: Out-of-sequence frame (REJ)
- B.3.2: Go-Back-N retransmission

### Planned Tests (C.1 - C.2)

**C.1: Multi-Peer**
- C.1.1: Two peers, independent windows
- C.1.2: Peer isolation (errors don't affect others)

**C.2: Stress Tests**
- C.2.1: Sustained high load
- C.2.2: Many errors (10% error rate)
- C.2.3: Rapid connect/disconnect

## Conclusion

This testing infrastructure provides:
- **Portability**: Same tests on Linux and ChibiOS
- **Determinism**: Mock streams, controllable errors
- **Coverage**: Unit, integration, scenario tests
- **Extensibility**: Easy to add new tests
- **Automation**: CI/CD ready

For more information, see:
- [Test Architecture](TEST_ARCHITECTURE.md)
- [Protocol Details](PROTOCOL.md)
