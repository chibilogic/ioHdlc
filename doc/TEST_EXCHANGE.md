# Exchange Test Tool

## Overview

The exchange test is a parametrized, long-running HDLC stress test for validating the entire protocol stack under realistic traffic conditions. It supports bidirectional communication, configurable error injection, latency and throughput measurement, and multiple duration modes.

The tool is fully cross-platform: the core logic lives in `tests/common/scenarios/test_exchange.c` with platform-specific wrappers for Linux (runtime CLI) and ChibiOS (compile-time defines or interactive shell).

## Quick Start

**Linux:**

```bash
make -C tests/linux
./tests/linux/build/bin/test_exchange --count=100 --size=120 --twa
```

**ChibiOS shell:**

```
iohdlc> exchange --count=100 --size=120 --twa
```

**ChibiOS standalone (compile-time):**

```bash
make -C tests/chibios exchange TEST_USE_TWA=1 TEST_DURATION_VALUE=100
```

## Command-Line Options

All options are available on Linux and in the ChibiOS shell. The ChibiOS standalone binary uses compile-time defines instead (see [ChibiOS Standalone Build](../tests/chibios/README_EXCHANGE.md)).

| Option | Default | Description |
|--------|---------|-------------|
| `--mode=MODE` | nrm | HDLC operating mode: `nrm`, `arm`, `abm` |
| `--twa` | _(off)_ | Use Two-Way Alternate |
| `--tws` | _(default)_ | Use Two-Way Simultaneous (explicit) |
| `--count=N` | 10 | Run for N iterations (sets count-based duration) |
| `--time=N` | -- | Run for N seconds (sets time-based duration) |
| `--exchanges=N` | 10 | Packets sent per iteration |
| `--size=N` | 64 | Packet size in bytes (max 120 for TYPE0 FFF) |
| `--direction=DIR` | both | Traffic direction: `pri2sec`, `sec2pri`, `both` |
| `--error-rate=N` | 0 | Error injection rate 0-100% (mock adapter only) |
| `--reply-timeout=N` | 0 (100ms) | HDLC reply timeout in ms |
| `--poll-retry-max=N` | 0 (5) | Max poll retries before link failure |
| `--progress-interval=N` | 1000 | Progress report interval in ms |
| `--watermark-delay=N` | 0 | Reader delay every 256 packets in ms (0=disabled) |
| `--help` | -- | Show usage |

## Configuration Parameters

### Mode and Link Type

- **Mode** (`--mode`): NRM (Normal Response Mode) is the default and most common. ARM and ABM are alternative ISO 13239 modes.
- **Link type** (`--twa`/`--tws`): TWS allows both stations to transmit independently. TWA alternates transmission turns via polling. SPI adapters require TWA.

### Duration

Three duration modes, mutually exclusive:

- **By count** (`--count=N`): run N iterations, each sending `--exchanges` packets per direction. Default mode.
- **By time** (`--time=N`): run for N seconds, sending continuously.
- **Infinite**: no `--count` or `--time` with very large values. Stop with Ctrl-C (Linux) or board reset (ChibiOS).

### Traffic Direction

- `both` (default): both stations send and receive simultaneously (4 active threads).
- `pri2sec`: primary sends only, secondary receives only (2 active threads).
- `sec2pri`: secondary sends only, primary receives only (2 active threads).

### Packet Size

Maximum 120 bytes for TYPE0 FFF framing. The 10-byte test packet header (sequence number + timestamp + payload length) is included in this size, leaving 110 bytes of user payload.

### Error Injection

Available only with the mock adapter. Sets random frame corruption at the specified percentage. Hardware adapters (UART, SPI) ignore this option.

@note At non-zero error rates, the protocol's checkpoint retransmission and (when supported) REJ recovery are exercised continuously.

### Watermark Testing

When `--watermark-delay` is non-zero, reader threads pause for the specified duration every 256 packets. This simulates a slow consumer, forcing the frame pool toward its LOW_WATER threshold and exercising backpressure callbacks.

### Protocol Tuning

- `--reply-timeout`: time the protocol waits for a response before retransmitting. Lower values increase retransmission aggressiveness. 0 uses the library default (100ms).
- `--poll-retry-max`: maximum retransmission attempts before declaring link failure. 0 uses the library default (5).

## Usage Examples

### Basic Tests

```bash
# 100 iterations, default settings (TWS, NRM, 64-byte packets, bidirectional)
./test_exchange --count=100

# 60-second test with TWA mode and large packets
./test_exchange --time=60 --twa --size=120

# Unidirectional: primary to secondary only
./test_exchange --count=500 --direction=pri2sec
```

### Stress Tests

```bash
# 5% error rate, long duration
./test_exchange --error-rate=5 --time=300 --exchanges=50

# High packet rate with short progress updates
./test_exchange --count=1000 --exchanges=100 --progress-interval=500

# Aggressive retransmission tuning
./test_exchange --error-rate=10 --reply-timeout=50 --poll-retry-max=10 --time=120
```

### Backpressure Testing

```bash
# 200ms reader delay every 256 packets
./test_exchange --watermark-delay=200 --count=500 --exchanges=50
```

## Output and Statistics

### Progress Reporting

The tool prints periodic updates depending on the duration mode:

**Count-based:**
```
Progress: 5000/10000 packets sent, 4998 rcv | PRI: 2500/2500 | SEC: 2500/2498
```

**Time-based:**
```
Elapsed: 30/60 seconds | PRI: 500 sent, 498 rcv | SEC: 498 sent, 500 rcv
```

**Infinite:**
```
Elapsed: 120 seconds | PRI: 1000 sent, 998 rcv | SEC: 995 sent, 1000 rcv
```

### Final Report

At completion, the tool prints per-station and per-direction statistics:

```
Primary Station:
  Packets sent:     30000
  Packets received: 29998
  Seq errors:       0
  Bytes sent:       1920000
  Bytes received:   1919872

Primary -> Secondary Traffic:
  Sent:       30000 packets (1920000 bytes)
  Received:   29998 packets (1919872 bytes)
  Lost:       2 packets (0.01%)
  Throughput: 32000.00 bytes/s (31.25 KB/s)
```

### Protocol Statistics

When compiled with `-DIOHDLC_ENABLE_STATISTICS`, additional per-peer counters are reported:

```
Protocol Statistics (Primary -> Secondary peer):
  REJ received:     2
  Checkpoints:      0
  Timeouts:         0
  Out of sequence:  0
```

## Packet Format

Each test packet contains a header for validation:

| Field | Size | Purpose |
|-------|------|---------|
| `sequence` | 4 bytes | Monotonic counter -- detects loss and reordering |
| `timestamp_ms` | 4 bytes | Transmission time -- measures latency |
| `payload_len` | 2 bytes | Payload size |
| `payload[]` | variable | Pattern bytes: `(sequence + offset) % 256` |

The receiver validates each packet against the expected sequence number, updating loss, reorder, and latency statistics.

## Threading Model

The tool creates 4 threads per test run:

| Thread | Role | Active when |
|--------|------|-------------|
| `pri_writer` | Primary station sends packets | `both` or `pri2sec` |
| `pri_reader` | Primary station receives packets | `both` or `sec2pri` |
| `sec_writer` | Secondary station sends packets | `both` or `sec2pri` |
| `sec_reader` | Secondary station receives packets | `both` or `pri2sec` |

Threads not needed for the selected direction exit immediately. Statistics are protected by per-station mutexes.

The global flag `test_running_global` coordinates shutdown: when any thread completes its work or encounters an error, all threads stop. On Linux, Ctrl-C sets `test_stop_requested` via signal handler.

## Adapter Support

| Adapter | Platform | Error Injection | Constraints |
|---------|----------|-----------------|-------------|
| Mock | Linux, ChibiOS | Yes (0-100%) | None |
| UART | ChibiOS | No | None |
| SPI | ChibiOS | No | TWA only (`ADAPTER_CONSTRAINT_TWA_ONLY`) |

The tool checks adapter constraints before starting. If a SPI adapter is selected with TWS mode, the tool prints an error and exits.

## Platform-Specific Details

### Linux

Runtime CLI via `getopt_long`. All options listed above are available. Ctrl-C triggers graceful shutdown.

```bash
./tests/linux/build/bin/test_exchange --help
```

### ChibiOS Standalone

Compile-time configuration via Makefile defines. See [ChibiOS Standalone Build](../tests/chibios/README_EXCHANGE.md) for the full list of `TEST_*` defines.

### ChibiOS Shell

Interactive shell with runtime CLI -- same syntax as Linux. See [ChibiOS Shell](../tests/chibios/README_SHELL.md) for shell-specific details.

## Debugging

### Station State Dump

On write/read errors, the tool automatically calls `test_dump_station_state()` which prints:

- Station configuration (address, mode, flags)
- Frame pool state (total/allocated/free frames, watermarks)
- Peer state (V(S), V(R), N(R), window size, state flags)
- Queue depths (TX, retransmit, RX)
- Timer state (T1/T3, armed/expired)

### Frame Tracing

Build with logging enabled to see individual frames:

```bash
make -C tests/linux clean
make -C tests/linux CFLAGS_EXTRA="-DIOHDLC_LOG_LEVEL=1"
./tests/linux/build/bin/test_exchange --count=5
```

### Protocol Counters

Build with `-DIOHDLC_ENABLE_STATISTICS` to get REJ, checkpoint, timeout, and out-of-sequence counters in the final report.
