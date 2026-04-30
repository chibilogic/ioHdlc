# Exchange Test Shell - ChibiOS Interactive Build

Interactive shell with runtime CLI for running the exchange test on ChibiOS targets. Same command-line syntax as the Linux version -- learn the options on Linux, use them identically on the embedded target.

For full tool documentation (all options, statistics, threading, examples), see [Exchange Test Tool](../../doc/TEST_EXCHANGE.md).

## Building

```bash
cd tests/chibios/stm32g474re

# Default (mock adapter)
make shell

# With UART adapter
make shell USE_UART_ADAPTER=1

# With SPI adapter
make shell USE_SPI_ADAPTER=1
```

**Output:** `build/iohdlc_shell.elf`

## Flashing and Connecting

```bash
# Flash
arm-none-eabi-gdb build/iohdlc_shell.elf
(gdb) target extended-remote :3333
(gdb) load
(gdb) continue

# Connect serial console (115200 baud, 8N1)
screen /dev/ttyACM0 115200
```

## Shell Commands

### exchange

Runs the HDLC exchange stress test. Accepts the same options as the Linux CLI:

```
iohdlc> exchange --count=100 --size=512 --twa
iohdlc> exchange --time=60 --error-rate=5 --direction=pri2sec
iohdlc> exchange --help
```

See [Exchange Test Tool -- Command-Line Options](../../doc/TEST_EXCHANGE.md) for the full option table.

### System Commands (ChibiOS built-in)

| Command | Description |
|---------|-------------|
| `help` | List available commands |
| `info` | System information (kernel version, architecture) |
| `mem` | Memory usage statistics |
| `threads` | Thread list with states and stack usage |
| `systime` | Display system ticks |

## Comparison with Other Binaries

| Binary | Purpose | Configuration | Interactive |
|--------|---------|---------------|-------------|
| `iohdlc_tests.elf` | Automated unit tests | N/A | No |
| `iohdlc_exchange.elf` | Exchange stress test | Compile-time defines | No |
| `iohdlc_shell.elf` | Exchange stress test | **Runtime CLI** | **Yes** |

The shell allows changing test parameters without recompilation.

## LED Indicator

Green LED (PA5) blinks at 1 Hz (100ms ON, 900ms OFF) to indicate the system is running.

## Shell Configuration

Edit the frontend-specific `conf/shellconf.h` to adjust:

- `SHELL_MAX_LINE_LENGTH` -- command line buffer
- `SHELL_MAX_ARGUMENTS` -- max argument count
- `SHELL_MAX_HIST_BUFF` -- history buffer size
- `SHELL_PROMPT_STR` -- prompt string
- `SHELL_USE_HISTORY`, `SHELL_USE_COMPLETION` -- feature toggles

## Memory Requirements

- **Stack:** 8 KB for shell thread
- **Heap:** 16-32 KB (depends on test parameters)
- **Flash:** ~80-100 KB (test scenarios + shell framework)

## See Also

- [Exchange Test Tool](../../doc/TEST_EXCHANGE.md) -- full documentation
- [ChibiOS Standalone Build](README_EXCHANGE.md) -- compile-time configuration
- [Adapter Architecture](README_ADAPTERS.md) -- mock/UART/SPI adapter details
