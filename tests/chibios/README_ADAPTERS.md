# ioHdlc Test Suite - Adapter Architecture

## Overview

Test suite supports two execution modes via adapter pattern:
- **Mock Adapter**: Memory-based, no hardware required
- **UART Adapter**: Hardware UART with physical connections

## Build Targets

### Mock Adapter (Default)
```bash
cd tests/chibios/stm32g474re
make clean
make              # or: make test-mock
```
**Size:** ~47KB text, ~48KB BSS

### UART Hardware Adapter  
```bash
cd tests/chibios/stm32g474re
make clean
make test-uart
```
**Size:** ~57KB text, ~31KB BSS

## Hardware Configuration (UART Adapter)

### Console
- Frontend-specific `TEST_OUTPUT_SD` at 115200 baud

### Endpoints
- Frontend-specific `TEST_ENDPOINT_A` - Endpoint A (Primary)
- Frontend-specific `TEST_ENDPOINT_B` - Endpoint B (Secondary)
- Both at 115200 baud, 8N1

**Physical Connections Required:**
```
UARTD2_TX  <──>  FUARTD1_RX
UARTD2_RX  <──>  FUARTD1_TX
```

## Board Configuration

Edit the frontend-specific `board_config/board_<frontend>.h` to change UART assignments:
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
  # or: UART Hardware
════════════════════════════════════════════════════════
```

## Adding New Boards

1. Create a dedicated frontend directory under `tests/chibios/`
2. Add `board_config/board_<name>.h` in that frontend
3. Define `TEST_OUTPUT_SD` and `TEST_ENDPOINT_A/B`
4. Add the frontend Makefile
