# ioHdlc Test Suite - ChibiOS/ARM

Complete test suite with ChibiOS RTOS.

## Prerequisites

1. **ARM GCC Toolchain**:
   - Location: `~/ARM/bin/arm-none-eabi-gcc`
   - Makefile uses absolute path (no PATH dependency)

2. **ChibiOS**:
   - Location: `../..`

## Build

### Compilation

```bash
cd tests/chibios
make
```

Output:
- `build/iohdlc_tests.elf` - ARM ELF executable
- `build/iohdlc_tests.bin` - Flash binary
- `build/iohdlc_tests.hex` - Hex format
- `build/iohdlc_tests.map` - Memory map

### Build Information

```bash
make info
```

Shows configuration, toolchain, included sources.

### Cleanup

```bash
make clean        # Cleans objects and binaries
make clean-all    # Cleans everything including ChibiOS files
```

## Test Structure

### OS-Agnostic Tests (Shared with Linux)

From `../common/scenarios/` directory:

1. **test_frame_pool.c**
   - `test_pool_init()` - Pool initialization
   - `test_take_release()` - Allocation/deallocation
   - `test_addref()` - Reference counting
   - `test_watermark()` - LOW/NORMAL thresholds
   - `test_exhaust_pool()` - Pool exhaustion

2. **test_basic_connection.c**
   - `test_station_creation()` - Station creation
   - `test_peer_creation()` - Peer creation
   - `test_snrm_handshake_frames()` - SNRM/UA handshake
   - `test_connection_timeout()` - Connection timeout

### Main Test Runner

The `main_tests.c` file creates a ChibiOS thread that:
1. Initializes HAL and RTOS
2. Activates serial driver (115200 baud) for output
3. Runs all tests in sequence
4. Shows results on serial

## Test Output

Output via UART0 (115200,n,8,1):

```
════════════════════════════════════════════════════════
  ioHdlc Test Suite - ChibiOS/ARM
════════════════════════════════════════════════════════

═══════════════════════════════════════════════
  Frame Pool Tests
═══════════════════════════════════════════════

🧪 Running test_pool_init...
✅ PASS: test_pool_init

🧪 Running test_take_release...
✅ PASS: test_take_release

...

═══════════════════════════════════════════════
  Final Summary
═══════════════════════════════════════════════
  Total Passed: 33
  Total Failed: 0
═══════════════════════════════════════════════

✅ All Tests Completed Successfully
```

## Deployment

## Linux/ChibiOS Compatibility

Tests in `common/scenarios/` are **100% portable**:

- Header `test_helpers.h` uses conditional macros:
  - Linux: `printf`, `nanosleep`
  - ChibiOS: `chprintf`, `chThdSleepMilliseconds`

- `IOHDLC_USE_CHIBIOS` defined in ChibiOS Makefile

- Same tests, same behavior, different platforms

### Limitations

1. **Not executable on x86**: ELF is for ARM, needs target hardware
2. **No file system**: Tests cannot save logs to file
3. **Serial output only**: Results visible only via UART

## Troubleshooting

**Error: "arm-none-eabi-gcc: command not found"**
- Check toolchain path in Makefile (line 211)
- Verify `~/ARM/bin/arm-none-eabi-gcc` exists

**Error: "No rule to make target ChibiOS..."**
- Verify `../..` exists
- Modify `CHIBIOS :=` in Makefile if needed

**Link errors with ChibiOS symbols**
- Verify all .mk files are included
- Check `conf/chconf.h` and `conf/halconf.h`

**Tests compile but don't run**
- Normal! They are for ARM, not x86
- Needs flash to real target and serial connection
