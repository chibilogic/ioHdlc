# Exchange Test - ChibiOS Standalone Build

Standalone binary with compile-time configuration. For full tool documentation (options, statistics, threading, examples), see [Exchange Test Tool](../../doc/TEST_EXCHANGE.md).

## Building

```bash
cd tests/chibios

# Default (mock adapter)
make exchange

# With UART adapter
make exchange USE_UART_ADAPTER=1

# With SPI adapter
make exchange USE_SPI_ADAPTER=1

# With custom parameters
make exchange \
  TEST_USE_TWA=1 \
  TEST_DURATION_TYPE=TEST_BY_TIME \
  TEST_DURATION_VALUE=60 \
  TEST_EXCHANGES=50 \
  TEST_PACKET_SIZE=120
```

**Output:** `build/iohdlc_exchange.elf`

## Compile-Time Defines

| Define | Default | Description |
|--------|---------|-------------|
| `TEST_MODE` | `IOHDLC_OM_NRM` | Operating mode (NRM, ARM, ABM) |
| `TEST_USE_TWA` | 0 | TWA mode (0=TWS, 1=TWA) |
| `TEST_DURATION_TYPE` | `TEST_BY_COUNT` | Duration: `TEST_BY_COUNT`, `TEST_BY_TIME`, `TEST_INFINITE` |
| `TEST_DURATION_VALUE` | 1000 | Iterations or seconds |
| `TEST_EXCHANGES` | 97 | Packets per iteration |
| `TEST_PACKET_SIZE` | 120 | Packet size in bytes (max 120) |
| `TEST_DIRECTION` | `TRAFFIC_BIDIRECTIONAL` | `TRAFFIC_PRI_TO_SEC`, `TRAFFIC_SEC_TO_PRI`, `TRAFFIC_BIDIRECTIONAL` |
| `TEST_ERROR_RATE` | 1 | Error injection 0-100% (mock adapter only) |
| `TEST_REPLY_TIMEOUT` | 0 | Reply timeout in ms (0=100ms default) |
| `TEST_POLL_RETRY_MAX` | 0 | Max retries (0=5 default) |
| `TEST_PROGRESS_INTERVAL` | 1000 | Progress update interval in ms |

## Flashing and Running

```bash
arm-none-eabi-gdb build/iohdlc_exchange.elf
(gdb) target extended-remote :3333
(gdb) load
(gdb) continue
```

Output goes to the serial console (115200 baud, 8N1).

## See Also

- [Exchange Test Tool](../../doc/TEST_EXCHANGE.md) -- full documentation
- [ChibiOS Shell](README_SHELL.md) -- interactive shell with runtime CLI
- [Adapter Architecture](README_ADAPTERS.md) -- mock/UART/SPI adapter details
