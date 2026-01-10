# ioHdlc Test Suite

Test infrastructure for ioHdlc organized in three testing levels.

## Directory Structure

```
tests/
├── common/                     # Shared across all platforms
│   ├── test_helpers.h/c        # Test framework
│   └── scenarios/              # OS-agnostic test scenarios
│       ├── test_basic.c              # Basic initialization/connection
│       ├── test_window_management.c  # Window control tests
│       ├── test_checkpoint_tws.c     # Checkpoint retransmission tests
│       └── ...                        # Future test scenarios
│
├── linux/                      # Linux/POSIX implementation
│   ├── mocks/                  # Mock implementations
│   │   ├── mock_stream.h       # Mock stream (simulates UART)
│   │   ├── mock_stream.c       # Buffer-based stream with loopback
│   │   └── tssi_stubs.c        # Test stubs
│   │
│   ├── main_tests.c            # Test runner
│   └── Makefile                # Builds common + linux tests
│
└── chibios/                    # ChibiOS/RT embedded implementation
    ├── mocks/                  # ChibiOS-specific mocks
    │   ├── mock_stream_chibios.h
    │   ├── mock_stream_chibios.c
    │   └── tssi_stubs.c
    │
    ├── main_tests.c            # ChibiOS test runner
    ├── Makefile                # ChibiOS build system
    └── README.md               # Platform-specific notes

os/
├── linux/                      # OSAL for Linux/POSIX
│   ├── include/
│   │   └── ioHdlcosal.h        # OSAL header (pthread, semaphores)
│   └── src/
│       ├── ioHdlcosal.c        # OSAL implementation
│       └── ioHdlcfmempool.c    # Frame pool implementation
│
└── chibios/                    # OSAL for ChibiOS
    ├── include/
    │   └── ioHdlcosal.h
    └── src/
        ├── ioHdlcosal.c
        └── ioHdlcfmempool.c
```

## Testing Philosophy

### OS-Agnostic Tests (`common/scenarios/`)

Tests that use only abstract interfaces, portable to any OS:
- **Frame Pool**: Uses only `hdlcTakeFrame`, `hdlcReleaseFrame`, `hdlcAddRef`
- **HDLC Core**: Tests stations, handshake, connection management
- **Stream Driver**: Tests abstract `ioHdlcStream` interface

These tests are compiled for each target platform but share the same source code.

### Platform-Specific Tests

Tests specific to OS implementation:
- **Linux**: Mock stream validation, OSAL verification
- **ChibiOS**: Tests with real hardware or virtual timers

## Testing Levels

### Level 1: Linux Mock (Current)

**Status**: ✅ Implemented
- POSIX OSAL with pthread/semaphore/mutex
- In-memory mock stream with circular buffers
- Thread-safe frame pool with free-list
- Test scenarios without hardware

**Advantages**:
- Fast development without HW dependencies
- Easy debugging (gdb, valgrind)
- CI/CD execution

### Level 2a: ChibiOS Mock

**Status**: ✅ Implemented
- Real ChibiOS OSAL (threads, semaphores, virtual timers)
- Mock stream simulating UART
- Tests on embedded target without physical hardware

### Level 2b: ChibiOS + Hardware

**Status**: ⏳ To be implemented
- Two UARTs on the same board
- Physical loopback (TX1→RX2, TX2→RX1)
- Real-world timing tests

**Note**: Allows protocol validation on real lines using a single board.

### Level 3: Core Unit Tests

**Status**: ⏳ To be implemented
- Isolated core logic tests
- Minimal mocks for stream/OSAL
- Focus on: frame parsing, sequence numbers, window logic

## Build and Execution

### Linux Tests

```bash
cd tests/linux

# Build all tests
make

# Build and run all tests
make test

# Clean artifacts
make clean
```

### Individual Tests

```bash
# Run only basic tests
./test_basic

# Run window management tests
./test_window_management

# Run checkpoint retransmission tests
./test_checkpoint_tws
```

### ChibiOS Tests

```bash
cd tests/chibios

# Build all tests
make clean
make

# Flash to target
# (See tests/chibios/README.md for platform-specific instructions)
```

## Debugging

### With GDB

```bash
gdb ./test_basic
(gdb) run
(gdb) bt
```

### With Valgrind (memory leaks)

```bash
valgrind --leak-check=full ./test_basic
```

### With Thread Sanitizer (race conditions)

```bash
# Recompile with sanitizer
make clean
CFLAGS_EXTRA="-fsanitize=thread" make
./test_basic
```

### Verbose Logging

```bash
# Compile with debug logging
make clean
CFLAGS_EXTRA="-DIOHDLC_LOG_LEVEL=4" make
./test_checkpoint_tws
```

## Implemented Test Scenarios

### 1. Basic Tests (`test_basic.c`)

- [x] **test_init_deinit**: Station initialization and cleanup
- [x] **test_snrm_handshake_frames**: SNRM → UA handshake

**Assertions**: 10+

### 2. Window Management (`test_window_management.c`)

- [x] **test_window_size_7**: Window size limits (modulo 8)
- [x] **test_window_slides_on_ack**: Window advancement on ACK

**Assertions**: 11+

### 3. Checkpoint Retransmission (`test_checkpoint_tws.c`)

#### A.1: Single Frame Loss

- [x] **test_A1_1_frame_loss_window_full**: Single frame loss with checkpoint recovery
  - Window size: 7
  - Error injection: Drop I1,0 (first transmission only)
  - Expected: 96 bytes received after retransmission

#### A.2: Multiple Frame Loss

- [x] **test_A2_1_multiple_frame_loss**: Frames 1 and 3 lost
  - Window size: 7
  - Error injection: Drop I1,0 and I3,0
  - Expected: 96 bytes received after retransmission

- [x] **test_A2_2_first_and_last_frame_loss**: Edge case - frames 0 and 7 lost
  - Window size: 7
  - Error injection: Drop I0,0 and I7,0
  - Expected: 96 bytes received after retransmission

**Assertions**: 45+ (15 per test)

**Total Assertions**: 33+ across all tests

## Error Injection Framework

### Mock Stream Error Filter

The mock stream supports selective frame corruption through callback filters:

```c
typedef bool (*mock_stream_error_filter_t)(uint32_t write_count,
                                            const uint8_t *data,
                                            size_t size,
                                            void *userdata);
```

**Parameters**:
- `write_count`: Number of frames written (all types: U, S, I)
- `data`: Frame data including flag, address, control, payload, FCS
- `size`: Total frame size
- `userdata`: Custom user data

**Return**:
- `true`: Corrupt this frame (flip FCS bits)
- `false`: Transmit normally

### Example: Drop Frame with N(S)=1

```c
static bool drop_frame_1_filter(uint32_t write_count,
                                 const uint8_t *data,
                                 size_t size,
                                 void *userdata) {
  static uint32_t corruption_count = 0;
  
  // Parse control byte (data[3] after flag, address, length)
  uint8_t control = data[3];
  uint8_t ns = (control >> 1) & 0x07;
  
  // Corrupt I-frame with N(S)=1, but only first transmission
  if (ns == 1 && corruption_count == 0) {
    corruption_count++;
    return true;  // Corrupt (flip FCS bits at size-3 and size-2)
  }
  
  return false;  // Transmit normally (allow retransmissions)
}
```

### Configuration

```c
mock_stream_config_t stream_config = {
  .loopback = false,
  .inject_errors = true,
  .error_rate = 1000,  // Ignored when filter provided
  .error_filter = drop_frame_1_filter,
  .error_userdata = NULL
};
```

## Planned Test Scenarios

### A.3: Window Management Edge Cases

- [ ] Wrap-around (N(S) 6→7→0)
- [ ] ACK out-of-order frames
- [ ] Window full with immediate ACK

### A.4: Flow Control

- [ ] RNR stops transmission
- [ ] RR resumes transmission
- [ ] RNR timeout handling

### A.5: Disconnect

- [ ] Clean disconnect (DISC → UA)
- [ ] Disconnect during transmission
- [ ] Reconnection after disconnect

### B.1: Error Detection

- [ ] Invalid FCS (discard frame)
- [ ] Invalid control byte (FRMR)
- [ ] Invalid N(R) (FRMR)
- [ ] Information field too long

### B.2: Timer Handling

- [ ] T1 timeout (retransmit)
- [ ] Max retries (link failure)
- [ ] T3 idle timeout (optional)

### B.3: REJ Recovery

- [ ] Out-of-sequence frame (REJ)
- [ ] Go-Back-N retransmission
- [ ] REJ during full window

### C.1: Multi-Peer

- [ ] Two peers, independent windows
- [ ] Peer isolation (errors don't affect others)
- [ ] Concurrent connections

### C.2: Stress Tests

- [ ] Sustained high load
- [ ] Many errors (10% error rate)
- [ ] Rapid connect/disconnect

### C.3: Concurrency & Thread Safety

- [ ] Multiple writers blocked/unblocked
- [ ] Read while window updating
- [ ] Disconnect during I/O
- [ ] Mutex correctness verification

## Adding New Tests

1. Create file in `common/scenarios/`:

```c
// test_my_scenario.c
#include "../../linux/mocks/mock_stream.h"
#include "../../../include/ioHdlc.h"
#include <stdio.h>
#include <string.h>

#define TEST_ASSERT(cond, msg) do { \
  if (!(cond)) { \
    printf("[FAIL] %s\n", msg); \
    return 1; \
  } \
} while (0)

int main(void) {
  printf("[TEST] test_my_scenario\n");
  
  // Setup
  ioHdlcSwDriver driver;
  ioHdlcStation station;
  
  // Initialize
  ioHdlcSwDriverInit(&driver);
  ioHdlcStationInit(&station, &config);
  
  // Test logic
  TEST_ASSERT(condition, "Description");
  
  // Cleanup
  ioHdlcStationDeinit(&station);
  
  printf("[PASS] test_my_scenario\n");
  return 0;
}
```

2. Add to `Makefile` (Linux):

```makefile
TEST_BINS += test_my_scenario

test_my_scenario: $(COMMON_DIR)/scenarios/test_my_scenario.c $(DEPS)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(SRCS) -o $@ $(LDFLAGS)
```

3. Build and run:

```bash
cd tests/linux
make
./test_my_scenario
```

## Next Steps

1. **Implement REJ error recovery tests**
   - Out-of-sequence detection
   - Go-Back-N retransmission
   - REJ timing

2. **Implement flow control tests**
   - RNR/RR sequences
   - Backpressure handling
   - Buffer management

3. **Implement disconnect tests**
   - DISC/UA sequence
   - Disconnect during I/O
   - Reconnection handling

4. **Implement stress tests**
   - High throughput
   - Many errors
   - Long-running tests

5. **Add hardware tests (ChibiOS)**
   - Physical UART loopback
   - Real-world timing
   - DMA integration

## Development Notes

- **Mock Stream**: Uses circular buffers in memory with pthread for synchronization
- **Peer Connection**: `mock_stream_connect()` allows bidirectional communication
- **Loopback**: Optional, TX automatically goes to RX for single-station tests
- **Error Injection**: ✅ Implemented - supports selective frame corruption via callback
- **Timing**: Configurable delay_us to simulate real latency (optional)
- **Platform Support**: ✅ Linux and ChibiOS fully supported

## Documentation

For more information, see:
- [Testing Guide](../doc/TESTING.md) - Comprehensive testing documentation
- [Test Architecture](../doc/TEST_ARCHITECTURE.md) - Test infrastructure details
- [Debug Guide](../doc/DEBUG_GUIDE.md) - Debugging tips and tools
- [Protocol Details](../doc/PROTOCOL.md) - HDLC protocol implementation
