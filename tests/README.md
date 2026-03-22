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

**Status**: ✅ Implemented
- UART adapter (`adapter_uart.c`): UARTD2/FUARTD1 physical loopback at 1.2 Mbaud
- SPI adapter (`adapter_spi.c`): master/slave, `ADAPTER_CONSTRAINT_TWA_ONLY`
- Conditional build: `USE_UART_ADAPTER` / `USE_SPI_ADAPTER` defines
- Same OS-agnostic scenarios run on real hardware

**Note**: Allows protocol validation on real lines using a single board.

### Level 3: Core Unit Tests

**Status**: ✅ Implemented
- Frame pool: 5 isolated tests (init, take/release, addref, watermark, exhaustion)
- OSAL binary semaphore: wait/signal semantics, timeout, reset, thread safety
- OSAL event system: broadcast, filtering, listeners, accumulation
- Mock stream: loopback, peer connection, error injection self-test

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

Configurable stress test with bidirectional traffic, error injection, latency/throughput measurement, and long-running support. See [Exchange Test Tool](../doc/TEST_EXCHANGE.md) for full documentation.

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

## Adding New Tests

See the [Testing Guide](../doc/TESTING.md) for a step-by-step walkthrough with accurate code examples covering:

1. Creating the scenario file in `common/scenarios/`
2. Declaring the entry point in `test_scenarios.h`
3. Creating a platform runner in `linux/test_runner_*.c`
4. Adding the target to the `Makefile`


## Development Notes

For design rationale and architectural details, see [Test Architecture](../doc/TEST_ARCHITECTURE.md).

- **Mock Stream**: Circular buffers with OSAL-based synchronization (mutexes, condition variables). Portable across Linux and ChibiOS.
- **Peer Connection**: `mock_stream_connect()` allows bidirectional communication
- **Loopback**: Optional, TX automatically goes to RX for single-station tests
- **Error Injection**: Selective frame corruption via callback filter (see [Error Injection Framework](#error-injection-framework))
- **Timing**: Configurable `delay_us` to simulate real latency (optional)
- **Platform Support**: Linux (POSIX) and ChibiOS fully supported, with mock and hardware adapters

## Documentation

For more information, see:
- [Testing Guide](../doc/TESTING.md) - Comprehensive testing documentation
- [Test Architecture](../doc/TEST_ARCHITECTURE.md) - Test infrastructure details
- [Protocol Details](../doc/PROTOCOL.md) - HDLC protocol implementation
