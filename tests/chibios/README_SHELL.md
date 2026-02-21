# ioHdlc Exchange Test Shell - ChibiOS Build

## Overview

The exchange test shell provides an interactive command-line interface for running the ioHdlc exchange stress test on ChibiOS/ARM targets with full Linux-compatible parameter support.

## Features

- **Linux-Compatible CLI**: Same command-line arguments as Linux version
- **Interactive Shell**: Run exchange tests with custom parameters interactively
- **ChibiOS Shell Integration**: Built on ChibiOS shell framework
- **History & Completion**: Command history and tab completion support
- **No ChibiOS Internal Tests**: ChibiOS's built-in tests (`test`, `files`) are disabled
- **System Commands**: Standard ChibiOS shell commands (info, mem, threads, etc.)

## Building

### Default Build (Mock Adapter)

```bash
make shell
# or
make shell-mock
```

Produces: `build/iohdlc_shell.elf` (Mock adapter, no hardware required)

### UART Adapter Build

```bash
make shell-uart
```

Produces: `build/iohdlc_shell.elf` (UART adapter, requires physical connections)

### Build Options

- `SHELL_TARGET=1`: Enable shell build (automatic with `make shell`)
- `USE_UART_ADAPTER=1`: Use UART hardware adapter instead of mock
- `CFLAGS_EXTRA`: Additional compiler flags

## Available Commands

### Exchange Test Command

**`exchange [options]`** - Run HDLC frame exchange stress test

#### Options (Linux-compatible)

```
--size=N           Frame payload size in bytes (default: 64)
--count=N          Run for N iterations (default: 100)
--exchanges=N      Exchanges per iteration (default: 10)
-p N               Poll interval in milliseconds (default: 1000)
--error-rate N     Error injection rate 0-100% (default: 0, mock adapter only)
--direction DIR    Exchange direction: both|a2b|b2a (default: both)
--reply-timeout N  Reply timeout in milliseconds (default: 100)
--help             Display usage information
```

#### Direction Options
- **`both`**: Bidirectional exchange (A→B and B→A simultaneously)
- **`a2b`**: Unidirectional A to B only
- **`b2a`**: Unidirectional B to A only

### System Commands (ChibiOS Built-in)

- **`help`** - List available commands
- **`info`** - System information (kernel version, etc.)
- **`mem`** - Memory usage statistics
- **`threads`** - Thread list with states and stack usage
- **`echo <text>`** - Echo text back
- **`systime`** - Display system ticks
- **`exit`** - Exit shell (restarts on embedded systems)

### Disabled Commands

The following ChibiOS built-in commands are disabled:
- **`test`** - ChibiOS RT/OSLIB internal tests (use standalone binaries instead)
- **`files`** - File system commands (not applicable)

## UBasic Exchange Test (Default Parameters)

```
iohdlc> exchange

ioHdlc Exchange Test
====================
Frame size: 64 bytes
Frames per exchange: 10
Number of exchanges: 100
Poll interval: 100 ms
Direction: both (bidirectional)
Reply timeout: 1000 ms
Error injection: 0%

Starting exchanges...
Exchange 1/100: A->B OK (789 µs), B->A OK (812 µs)
Exchange 2/100: A->B OK (791 µs), B->A OK (809 µs)
...
```

### Custom Parameters

```
iohdlc> exchange --size=120 --count=50 --exchanges=200

ioHdlc Exchange Test
====================
Frame size: 120 bytes
Frames per exchange: 50
Number of exchanges: 200
...
```

### Quick Test with Short Poll Interval

```
iohdlc> exchange --size=64 --count=10 --exchanges=10 -p50

Running quick test with 50ms poll interval...
```

### Unidirectional Test

```
iohdlc> exchange --direction=a2b --count=100
Testing A to B direction only...
```

### Error Injection (Mock Adapter Only)

```
iohdlc> exchange --error-rate 5 --exchanges=50
Injecting 5% error rate for stress testing...
```

### Full Parameter Example

```
iohdlc> exchange --size=128 --count=20 --exchanges=500 -p100 --error-rate 2 --direction=both --reply-timeout=2000

Running comprehensive stress test...
```

### Get Help

```
iohdlc> exchange --help

Usage: exchange [options]
Options:
  --size=N           Frame size (default: 64)
  --count=N          Frames per exchange (default: 10)
  --exchanges=N      Number of exchanges (default: 100)
  -p N               Poll interval ms (default: 100)
  --error-rate N     Error rate 0-100% (default: 0)
  --direction DIR    Direction: both|a2b|b2a (default: both)
  --reply-timeout N  Timeout ms (default: 1000)
  --help             Show this help
```

### System Commands

```
iohdlc> info
Kernel:       ChibiOS/RT
Compiler:     GCC 10.3.1
Architecture: ARMv7E-M

iohdlc> mem
core free memory : 24576 bytes
heap fragments   : 0
heap free total  : 16384 bytes

iohdlc> threads
    addr    stack prio refs     state     time    name
0x20000400  0x128   64    1  CURRENT        0  main
0x20000800  0x080   63    1     READY        5  blinker
iohdlc> mem
core free memory : 12345 bytes
```

## Shell Configuration
 (115200 baud)
  ├─ shellInit()                       // Shell initialization
  ├─ chThdCreateStatic(..., blinker)  // LED blinker thread
  └─ shellThread(&shell_cfg)          // Shell (main thread)

shell_cfg
  ├─ sc_channel: &SD2                 // Serial stream
  └─ sc_commands: commands[]          // Custom command table

commands[]
  └─ {"exchange", cmd_exchange}       // Exchange test command
```

### Command Implementation

The `cmd_exchange` function has signature:
```c
void cmd_exchange(BaseSequentialStream *chp, int argc, char *argv[]);
```

It directly passes shell arguments to `test_exchange_main()`:
```c
static void cmd_exchange(BaseSequentialStream *chp, int argc, char *argv[]) {
  (void)chp;
  test_exchange_main(argc, argv);  // Linux-compatible!
}
```

This provides **full compatibility** with the Linux version - same arguments, same parsing logic, same behavior.
main()
  ├─ halInit() / chSysInit()          // System initialization
  ├─ sdStart(&SD2, &sdcfg)            // Serial port setup
  ├─ shellInit()                       // Shell initialization
  ├─ chThdCreateStatic(..., blinker)  // LED blinker thread
  └─ shellThread(&shell_cfg)          // Shell (main thread)

shell_cfg
  ├─ sc_channel: &SD2                 // Serial stream
  └─ sc_commands: commands[]          // Custom command table

commands[]
  ├─ {"tests",      cmd_tests}        // All tests
  ├─ {"exchange",   cmd_exchange}     // Exchange test
  ├─ {"framepool",  cmd_framepool}    // Pool tests
  ├─ {"checkpoint", cmd_checkpoint}   // Checkpoint tests
  └─ {"adapter",    cmd_adapter}      // Adapter info
```

### Command FunctionsParameters | Adapter | Interactive |
|--------|---------|------------|---------|-------------|
| `iohdlc_tests.elf` | Unit tests suite | Compile-time | Mock/UART | No |
| `iohdlc_exchange.elf` | Stress test | Compile-time | Mock/UART | No |
| `iohdlc_shell.elf` | Exchange shell | **Runtime CLI** | Mock/UART | **Yes** |

**Key Advantage**: `iohdlc_shell.elf` allows changing test parameters without recompilation!
```

Commands use:
- `chprintf(chp, ...)` for output to serial
- `RUN_TEST(func)` macro for test execution
- `TEST_ADAPTER->init()/deinit()` for adapter lifecycle

## Memory Requirements

- **Stack**: 8KB for shell thread (configured in `SHELL_WA_SIZE`)
- **Heap**: Depends on test scenarios (typically 16-32KB)
- **Flash**: ~80-100KB (includes all test scenarios + shell framework)

## Serial Configuration

Default serial settings (115200 8N1):
```c
static const SerialConfig sdcfg = {
  .speed = 115200
};
```

Adjust in [main_shell.c](main_shell.c) if needed.

## Comparison with Other Binaries

| Binary | Purpose | Scenarios | Adapter | Interactive |
|--------|---------|-----------|---------|-------------|
| `iohdlc_tests.elf` | Unit tests | Frame pool, connections, checkpoints | Mock/UART | No |
| `iohdlc_exchange.elf` | Stress test | Exchange only | Mock/UART | No |
| `iohdlc_shell.elf` | Interactive | All scenarios | Mock/UART | **Yes** |

## Flashing & Running

### Flash to Target
```bash
# Using st-flash (STM32)
st-flash write build/iohdlc_shell.bin 0x08000000

# Using OpenOCD
openocd -f board/sRuns Forever
- Exchange test runs for the specified number of exchanges (default: 100)
- If set to very large number, may appear to run forever
- No way to interrupt from shell (limitation)
- Reset board to restart shell if needed
```bash
# Linux
screen /dev/ttyACM0 115200

# macOS
screen /dev/tty.usbmodem* 115200

# Or use USB CDC serial terminal (e.g., minicom, picocom, CuteCom)
```

### LED Indicators

- **Green LED (PA5)**: Blinks at 1Hz when system is running
  - 100ms ON, 900ms OFF
  - Indicates system health

## Troubleshooting

### Command Not Found
- Type `help` to list available commands
- Shell is case-sensitive: use lowercase

### No Output After Flash
- Check serial port settings (115200 baud, 8N1)
- Verify correct COM port (/dev/ttyACM0 on Linux)
- Try pressing RESET button on board

### Exchange Test Won't Stop
- Exchange test runs until manually stopped
- Press Ctrl+C in terminal (serial break signal)
- Or reset board to restart shell

###Linux Compatibility

The shell provides **full parameter compatibility** with the Linux version:

### Same Command Syntax
```bash
# Linux
$ ./test_exchange --size=120 --count=50 --exchanges=100 -p500

# ChibiOS Shell (identical!)
iohdlc> exchange --size=120 --count=50 --exchanges=100 -p500
```

### Same Argument Parsing
- Both use the same `test_exchange_main(argc, argv)` function
- Portable implementation in `tests/common/scenarios/test_exchange.c`
- No `#ifdef` conditionals for platform detection

### Benefits
- **No Surprises**: Learn command options on Linux, use on embedded
- **Documentation**: Single set of docs for all platforms
- **Testing**: Validate on Linux before flashing to hardware

## Development

### Adding New Commands

To add additional test commands (besides `exchange`):

1. Ensure test function accepts `(int argc, char **argv)` signature:
   ```c
   extern int test_mytest_main(int argc, char **argv);
   ```

2. Create shell command wrapper:
   ```c
   static void cmd_mytest(BaseSequentialStream *chp, int argc, char *argv[]) {
     (void)chp;
     test_mytest_main(argc, argv);
   }
   ```

3. Add to `commands[]` array:
   ```c
   {"mytest", cmd_mytest},
   ```

### Modifying Shell Behavior

Edit [conf/shellconf.h](conf/shellconf.h) to adjust:
- Line length: `SHELL_MAX_LINE_LENGTH`
- Max arguments: `SHELL_MAX_ARGUMENTS`
- History size: `SHELL_MAX_HIST_BUFF`
- Prompt string: `SHELL_PROMPT_STR`
- Feature toggles: `SHELL_USE_HISTORY`, `SHELL_USE_COMPLETION`onf.h](conf/shellconf.h) to adjust:
- Line length limits
- History buffer size
- Prompt string
- Feature enables

## See Also

- [TEST_ARCHITECTURE.md](../../doc/TEST_ARCHITECTURE.md) - Test framework architecture
- [TESTING.md](../../doc/TESTING.md) - General testing documentation
- [README_EXCHANGE.md](README_EXCHANGE.md) - Exchange test documentation
- [ChibiOS Shell Documentation](http://www.chibios.org/dokuwiki/doku.php?id=chibios:kb:shell)
