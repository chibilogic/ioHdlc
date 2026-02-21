# ioHdlc Test Exchange - ChibiOS Build Guide

## Overview

The exchange test (`test_exchange.c`) is a parametrized long-running test for stress-testing the HDLC protocol with configurable traffic patterns.

## Building for ChibiOS

The ChibiOS Makefile now supports two separate binaries:

### 1. Unit Tests Suite (default)
```bash
cd tests/chibios
make tests          # Build iohdlc_tests.elf
# or
make all            # Default behavior
```

**Output**: `build/iohdlc_tests.elf`
**Contains**: All unit tests (frame pool, basic connection, checkpoint tests, etc.)  
**Main file**: `main_tests.c`

### 2. Exchange Test
```bash
cd tests/chibios
make exchange       # Build iohdlc_exchange.elf
```

**Output**: `build/iohdlc_exchange.elf`  
**Contains**: Parametrized exchange test only  
**Main file**: `main_exchange.c`

## Configuration

### Test Parameters

Exchange test parameters are configured at **compile-time** via Makefile defines (see `test_config_chibios.c`):

```makefile
# Example: Build exchange test with custom parameters  
make exchange \
  TEST_MODE=IOHDLC_OM_NRM \
  TEST_USE_TWA=1 \
  TEST_DURATION_TYPE=TEST_BY_TIME \
  TEST_DURATION_VALUE=60 \
  TEST_EXCHANGES=50 \
  TEST_PACKET_SIZE=120 \
  TEST_DIRECTION=TRAFFIC_BIDIRECTIONAL
```

### Available Defines

| Define | Description | Default |
|--------|-------------|---------|
| `TEST_MODE` | Operating mode (IOHDLC_OM_NRM, ARM, ABM) | IOHDLC_OM_NRM |
| `TEST_USE_TWA` | Use Two-Way Alternate (0=TWS, 1=TWA) | 0 |
| `TEST_DURATION_TYPE` | TEST_BY_COUNT, TEST_BY_TIME, TEST_INFINITE | TEST_BY_COUNT |
| `TEST_DURATION_VALUE` | Iterations or seconds | 10 |
| `TEST_EXCHANGES` | Exchanges per iteration | 97 |
| `TEST_PACKET_SIZE` | Packet size in bytes (max 120) | 120 |
| `TEST_DIRECTION` | TRAFFIC_PRI_TO_SEC, SEC_TO_PRI, BIDIRECTIONAL | BIDIRECTIONAL |
| `TEST_ERROR_RATE` | Error injection 0-100% | 0 |
| `TEST_REPLY_TIMEOUT` | Reply timeout in ms (0=100ms default) |  0 |
| `TEST_POLL_RETRY_MAX` | Max poll retries (0=5 default) | 0 |
| `TEST_PROGRESS_INTERVAL` | Progress update interval in ms | 1000 |

## Flashing & Running

```bash
# Flash unit tests
make tests
arm-none-eabi-gdb build/iohdlc_tests.elf
(gdb) target extended-remote :3333
(gdb) load
(gdb) continue

# Flash exchange test
make exchange  
arm-none-eabi-gdb build/iohdlc_exchange.elf
(gdb) target extended-remote :3333
(gdb) load
(gdb) continue
```

## Adapter Selection

Both binaries support mock or UART adapters:

```bash
# Mock adapter (default - no hardware needed)
make tests          # or make exchange

# UART adapter (requires wired connections)
make tests USE_UART_ADAPTER=1
make exchange USE_UART_ADAPTER=1
```

## Clean

```bash
make clean          # Clean build artifacts
make clean-all      # Clean everything including .dep/
```

## Linux Build (for comparison)

Linux uses runtime command-line arguments instead of compile-time defines:

```bash
cd tests/linux
make
./build/bin/test_exchange --help
./build/bin/test_exchange --mode=nrm --twa --count=100 --exchanges=50 --size=64
```

See `tests/linux/test_config_linux.c` for full CLI options.
