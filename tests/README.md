# ioHdlc Test Suite

Test infrastructure for ioHdlc organized in three testing levels.

## Directory Structure

```
tests/
├── common/                     # Shared across all platforms
│   ├── adapters/               # Adapter abstraction for test scenarios
│   │   ├── adapter_interface.h       # Abstract adapter interface
│   │   └── adapter_mock.h/c          # Mock adapter (in-process loopback)
│   ├── mocks/                  # Mock stream implementations
│   │   ├── mock_stream.h/c           # Core mock stream (byte-level)
│   │   └── mock_stream_adapter.h/c   # Adapter wrapper over mock stream
│   ├── scenarios/              # OS-agnostic test scenarios
│   │   ├── test_basic_connection.c       # Station/peer creation, SNRM handshake, data exchange (TWS)
│   │   ├── test_basic_connection_twa.c   # Data exchange in TWA mode
│   │   ├── test_checkpoint_tws.c         # Checkpoint retransmission (TWS)
│   │   ├── test_checkpoint_twa.c         # Checkpoint retransmission (TWA)
│   │   ├── test_exchange.c               # Parametrized bidirectional throughput test
│   │   └── test_frame_pool.c             # Frame pool allocation, refcount, watermarks
│   ├── test_arenas.h/c         # Pre-allocated memory arenas for tests
│   ├── test_framework.h/c      # Assertion and reporting primitives
│   ├── test_helpers.h/c        # Station/peer setup helpers
│   └── test_scenarios.h        # Scenario entry-point declarations
│
├── linux/                      # Linux/POSIX test runners
│   ├── scenarios/              # Linux-specific standalone tests
│   │   ├── test_mock_stream.c        # Mock stream self-test
│   │   ├── test_osal_bsem.c          # OSAL binary semaphore tests
│   │   └── test_osal_events.c        # OSAL event system tests
│   ├── test_config_linux.c     # Linux adapter/runner factory used by all test runners
│   ├── test_runner_*.c         # One entry-point file per scenario (links common scenario)
│   └── Makefile                # Builds all binaries into build/bin/
│
└── chibios/                    # ChibiOS/RT target
    ├── adapters/               # Hardware adapters (UART, SPI)
    │   ├── adapter_uart.c
    │   └── adapter_spi.c
    ├── board_config/           # STM32/Nucleo board files
    ├── conf/                   # ChibiOS kernel and HAL configuration
    ├── main_tests.c            # Automated test runner (flashed, runs on target)
    ├── main_exchange.c         # Interactive exchange entry point
    ├── main_shell.c            # Serial shell entry point
    ├── test_config_chibios.c   # ChibiOS adapter/runner factory
    ├── Makefile
    └── README*.md              # Platform-specific notes
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

Built binaries are placed in `tests/linux/build/bin/`.

```bash
cd tests/linux

# Protocol tests
./build/bin/test_basic_connection
./build/bin/test_basic_connection_twa
./build/bin/test_checkpoint_tws
./build/bin/test_checkpoint_twa
./build/bin/test_frame_pool

# Parametrized exchange test
./build/bin/test_exchange --help

# OSAL and mock infrastructure tests
./build/bin/test_osal_bsem
./build/bin/test_osal_events
./build/bin/test_mock_stream
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
cd tests/linux
gdb ./build/bin/test_basic_connection
(gdb) run
(gdb) bt
```

### With Valgrind (memory leaks)

```bash
cd tests/linux
valgrind --leak-check=full ./build/bin/test_basic_connection
```

### With Thread Sanitizer (race conditions)

```bash
cd tests/linux
make clean
CFLAGS_EXTRA="-fsanitize=thread" make
./build/bin/test_basic_connection
```

### Verbose Logging

Log levels 1–3: 1 = frame summaries, 2 = frame data, 3 = full hex dump.

```bash
cd tests/linux
make clean
CFLAGS_EXTRA="-DIOHDLC_LOG_LEVEL=2" make
./build/bin/test_checkpoint_tws
```

## Implemented Test Scenarios

### 1. Basic Connection — TWS (`test_basic_connection.c`)

- [x] **test_station_creation**: Station initialisation and peer setup
- [x] **test_peer_creation**: Peer registration and address validation
- [x] **test_snrm_handshake**: SNRM → UA handshake, link-up event
- [x] **test_data_exchange**: Bidirectional byte-stream transfer over an established TWS link

### 2. Basic Connection — TWA (`test_basic_connection_twa.c`)

- [x] **test_data_exchange_twa**: Bidirectional transfer in TWA (Two-Way Alternate) mode

### 3. Frame Pool (`test_frame_pool.c`)

- [x] **test_pool_init**: Pool initialisation and capacity reporting
- [x] **test_take_release**: Allocate and release cycles
- [x] **test_addref**: Reference-count increment and deferred release
- [x] **test_watermark**: LOW/NORMAL watermark callback triggering
- [x] **test_exhaust_pool**: Behaviour when pool is fully allocated

### 4. Checkpoint Retransmission — TWS (`test_checkpoint_tws.c`)

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

### 5. Checkpoint Retransmission — TWA (`test_checkpoint_twa.c`)

- [x] **test_A1_1_frame_loss_window_full_twa**: Single I-frame loss with checkpoint recovery in TWA mode
- [x] **test_A2_1_multiple_frame_loss_twa**: Two non-adjacent I-frame losses in TWA mode
- [x] **test_A2_2_first_and_last_frame_loss_twa**: First and last I-frame of a window lost in TWA mode

### 6. Parametrized Exchange (`test_exchange.c`)

Configurable throughput test with real bidirectional traffic, runtime statistics, and parametrized frame loss injection. Run with `./build/bin/test_exchange --help` for all options.

### 7. OSAL Tests (Linux-specific)

- [x] **test_osal_bsem** (`linux/scenarios/test_osal_bsem.c`): Binary semaphore wait/signal semantics
- [x] **test_osal_events** (`linux/scenarios/test_osal_events.c`): Event source broadcast and listener wake-up
- [x] **test_mock_stream** (`linux/scenarios/test_mock_stream.c`): Mock stream loopback and error injection self-test

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
- [Protocol Details](../doc/PROTOCOL.md) - HDLC protocol implementation
