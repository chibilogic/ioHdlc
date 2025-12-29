# ioHdlc Test Suite - Adapter Architecture

## Overview

Test suite supports two execution modes via adapter pattern:
- **Mock Adapter**: Memory-based, no hardware required
- **UART Adapter**: Hardware UART with physical connections

## Build Targets

### Mock Adapter (Default)
```bash
make clean
make              # or: make test-mock
```
**Size:** ~47KB text, ~48KB BSS

### UART Hardware Adapter  
```bash
make clean
make test-uart
```
**Size:** ~57KB text, ~31KB BSS

## Hardware Configuration (UART Adapter)

### Console
- **SD1** - Test output at 115200 baud

### Endpoints
- **UARTD2** - Endpoint A (Primary)
- **FUARTD1** - Endpoint B (Secondary)  
- Both at 115200 baud, 8N1

**Physical Connections Required:**
```
UARTD2_TX  <──>  FUARTD1_RX
UARTD2_RX  <──>  FUARTD1_TX
```

## Board Configuration

Edit `board_config/board_sama5d2x.h` to change UART assignments:
```c
#define TEST_OUTPUT_SD    SD1      // Console
#define TEST_ENDPOINT_A   UARTD2   // Primary
#define TEST_ENDPOINT_B   FUARTD1  // Secondary
```

## Test Output

Both adapters show adapter name in banner:
```
════════════════════════════════════════════════════════
  ioHdlc Test Suite - ChibiOS/ARM
════════════════════════════════════════════════════════
  Adapter: Mock Stream (memory-based)
  # or: UART Hardware (UARTD2 + FUARTD1)
════════════════════════════════════════════════════════
```

## Adding New Boards

1. Create `board_config/board_<name>.h`
2. Define TEST_OUTPUT_SD and TEST_ENDPOINT_A/B  
3. Add conditional include in `board_config.h`
4. Add `-DBOARD_<NAME>` to Makefile

