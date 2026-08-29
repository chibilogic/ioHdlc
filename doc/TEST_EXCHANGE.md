# Exchange Test Tool

## Overview

The exchange test is a parametrized, long-running HDLC stress test for validating the entire protocol stack under realistic traffic conditions. It supports bidirectional communication, configurable error injection, latency and throughput measurement, and multiple duration modes.

It can also run a TEST command/response check using `--test[=N]`. In that
mode the tool behaves like a small HDLC ping: it sends N disconnected TEST
commands, waits for the peer TEST responses, then terminates.

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
make -C tests/chibios/stm32g474re exchange TEST_MODULO=128 TEST_USE_TWA=1 TEST_DURATION_VALUE=100
```

## Command-Line Options

All options below are available on Linux and in the ChibiOS shell. The ChibiOS standalone binary uses compile-time defines instead (see [ChibiOS Standalone Build](../tests/chibios/README_EXCHANGE.md)).

Unless noted otherwise, the semantics are identical on Linux and in the shell. The defaults shown in the table are the Linux runtime defaults; ChibiOS-specific differences are listed in [Platform-Specific Defaults](#platform-specific-defaults).

| Option | Default | Description |
|--------|---------|-------------|
| `--mode=MODE` | nrm | HDLC operating mode: `nrm`, `abm` |
| `--modulo=N` | 8 | HDLC modulo: `8`, `128` |
| `--twa` | _(off)_ | Use Two-Way Alternate |
| `--tws` | _(default)_ | Use Two-Way Simultaneous (explicit) |
| `--count=N` | 10 | Run for N iterations (sets count-based duration) |
| `--time=N` | -- | Run for N seconds (sets time-based duration) |
| `--test[=N]` | -- | Run N TEST command/response cycles, or 1 cycle when N is omitted |
| `--exchanges=N` | 10 | Packets sent per iteration |
| `--size=N` | 64 | Packet size in bytes, including the 10-byte test header (range: 10-1024) |
| `--rand-size=N` | off | Random packet-size seed; each packet is 10-120 bytes (alias: `--rand_size=N`) |
| `--direction=DIR` | both | Traffic direction: `pri2sec`, `sec2pri`, `both` |
| `--endpoint=EP` | both | Local endpoint selection: `both`, `a`, `b` (aliases: `primary`, `secondary`) |
| `--error-rate=N` | 0 | Error injection rate 0-100% (mock adapter only) |
| `--reply-timeout=N` | `0` (`IOHDLC_REPLY_TIMEOUT_MS_DEFAULT`) | HDLC reply timeout in ms |
| `--idle-poll-timeout=N` | `0` (`2*T1`) | NRM idle-poll T3 in ms; explicit range `T1`-65535 |
| `--poll-retry-max=N` | `0` (auto) | Max poll retries before link failure, range 0-31; `0` auto-calculates it from `--reply-timeout` for about 25 s cumulative retry timeout |
| `--krs=N` | modmask | Window size (`ks = kr = N`) |
| `--progress-interval=N` | 1000 | Progress report interval in ms |
| `--watermark-delay=N` | 0 | Reader delay every 256 packets in ms (0=disabled) |
| `--help` | -- | Show usage |

Shell-only shorthand:
- `-p N` is equivalent to `--progress-interval=N`
- `-w N` is equivalent to `--watermark-delay=N`

## Configuration Parameters

### Mode and Link Type

- **Mode** (`--mode`): NRM (Normal Response Mode) is the default and most common mode, while ABM is the primary choice for point-to-point links using TWS.
- **Link type** (`--twa`/`--tws`): TWS allows both stations to transmit independently. TWA alternates transmission turns via polling. SPI adapters require TWA and NRM.
- **Modulo** (`--modulo`): modulo 8 is the default. Use modulo 128 to exercise extended control fields and larger sequence spaces.

### Duration

Three duration modes, mutually exclusive:

- **By count** (`--count=N`): run N iterations, each sending `--exchanges` packets per direction. Default mode.
- **By time** (`--time=N`): run for N seconds, sending continuously.
- **Infinite**: no `--count` or `--time` with very large values. Stop with Ctrl-C (Linux) or board reset (ChibiOS).

### TEST Command Mode

`--test[=N]` switches the tool from data exchange to TEST command mode. The
tool initializes the selected endpoint(s), starts the HDLC runner(s), sends N
TEST command/response cycles from endpoint A, then exits. `--test` without a
value is equivalent to `--test=1`.

TEST command mode runs while the link is disconnected; it does not perform
`LinkUp` and does not start the exchange reader/writer threads. The valid
runtime options in this mode are only `--test[=N]`, `--size=N`,
`--endpoint=both|a|b`, and `--help`.

In TEST command mode, `--size` is the TEST information field length. It is not
the exchange packet size and does not include the 10-byte exchange test header.
`--size=0` is valid for TEST because the TEST information field is optional.

With `--endpoint=b`, the tool runs endpoint B as a responder-only TEST target.
It stays active for the same protocol I/O guard window used by the exchange
tool, then terminates cleanly. With default timing this window is about 27 s.

### Traffic Direction

- `both` (default): both stations send and receive simultaneously (4 active threads).
- `pri2sec`: primary sends only, secondary receives only (2 active threads).
- `sec2pri`: secondary sends only, primary receives only (2 active threads).
- ChibiOS shell also accepts `a2b` and `b2a` as aliases for `pri2sec` and `sec2pri`.

Endpoint names are test-harness labels. In NRM, endpoint A is the primary and
endpoint B is the secondary. In ABM, A/B identify the two local test endpoints;
there is no NRM-style primary/secondary relationship.

### Local Endpoint Selection

`--endpoint` selects which endpoint is instantiated locally:

- `both` (default): instantiate endpoint A and endpoint B in the same process or firmware image.
- `a`: instantiate only endpoint A locally; endpoint B must run on another process or target.
- `b`: instantiate only endpoint B locally; endpoint A must run on another process or target.
- `primary` and `secondary` are accepted as aliases for `a` and `b`.

Traffic direction still describes the logical flow between A and B. For example,
run `--endpoint=a --direction=a2b` on the A side and
`--endpoint=b --direction=a2b` on the B side for a two-target A-to-B test.

The aliases `primary` and `secondary` follow the NRM mapping: `primary` means
endpoint A, and `secondary` means endpoint B.

### Packet Size

`--size` is the total size of the test packet passed to `ioHdlcWriteTmo()`, including the 10-byte test header (sequence number + timestamp + payload length). The exchange harness supports packet sizes from 10 to 1024 bytes.

This is intentionally larger than a single HDLC I-frame on TYPE0 FFF links, so values above `mifls` exercise the writer fragmentation path instead of being rejected by the test harness.

In `--test` mode, `--size` has different semantics: it is the TEST information
field length and may be zero.

`--rand-size=N` enables deterministic random packet sizes using `N` as the seed. In this mode each write is between 10 and 120 bytes, and completion is still based on packet counts rather than byte totals.

### Error Injection

Available only with the mock adapter. Sets random frame corruption at the specified percentage. Hardware adapters (UART, SPI) ignore this option.

@note At non-zero error rates, the protocol's checkpoint retransmission and (when supported) REJ recovery are exercised continuously.

### Watermark Testing

When `--watermark-delay` is non-zero, reader threads pause for the specified duration every 256 packets. This simulates a slow consumer, forcing the frame pool toward its LOW_WATER threshold and exercising backpressure callbacks.

### Protocol Tuning

- `--reply-timeout`: time the protocol waits for a response before retransmitting. Lower values increase retransmission aggressiveness. `0` uses `IOHDLC_REPLY_TIMEOUT_MS_DEFAULT`.
- `--idle-poll-timeout`: NRM idle-poll interval. `0` uses `2*T1`; an explicit value must be between the resolved T1 and 65535 ms. Non-zero values are rejected in ABM.
- `--poll-retry-max`: maximum retransmission attempts before declaring link failure. `0` makes the exchange tool choose the value from `--reply-timeout`.
- `--reply-timeout` and `--poll-retry-max` interact geometrically, not linearly: reply-timeout recovery doubles T1 before each retry. After the last retry, the response window is bounded to `max(IOHDLC_LAST_RETRY_T1_RATIO * reply_timeout, IOHDLC_LAST_RETRY_TIMEOUT_MIN_MS)`. With `--poll-retry-max=0`, the tool chooses N2 so the cumulative timeout stays close to 25 s. An explicit non-zero value bypasses this auto calculation.
- Hardware adapters may also use the resolved `--reply-timeout` value to tune backend-local guard timings. For example, the ChibiOS SPI adapter derives its slave RX/TX watchdog from T1 so stale SPI transactions are recovered before the HDLC retry window expires.
- `--krs`: sets both `ks` and `kr`. The value must be at least 1 and no larger than the modmask of the selected modulo (`7` for modulo 8, `127` for modulo 128).

## Platform-Specific Defaults

- Linux runtime defaults: `--count=10`, `--mode=nrm`, `--modulo=8`, `--tws`, `--endpoint=both`, `--reply-timeout=0`, `--idle-poll-timeout=0`, `--poll-retry-max=0`
- ChibiOS shell defaults: `--count=100`, `--mode=nrm`, `--modulo=8`, `--tws`, `--endpoint=both`, `--reply-timeout=0`, `--idle-poll-timeout=0`, `--poll-retry-max=0`
- ChibiOS standalone defaults are compile-time:
  `TEST_MODE=IOHDLC_OM_NRM`, `TEST_MODULO=8`, `TEST_USE_TWA=0`, `TEST_ENDPOINT=TEST_ENDPOINT_BOTH`, `TEST_DURATION_TYPE=TEST_BY_COUNT`, `TEST_DURATION_VALUE=1000`

## Usage Examples

### Basic Tests

```bash
# 100 iterations, default settings (TWS, NRM, 64-byte packets, bidirectional)
./test_exchange --count=100

# 60-second test with TWA mode and large packets
./test_exchange --time=60 --twa --size=120

# Unidirectional: primary to secondary only
./test_exchange --count=500 --direction=pri2sec

# Run only endpoint A locally, with endpoint B on another target/process
./test_exchange --endpoint=a --direction=a2b --count=500

# ABM/TWS with modulo 128
./test_exchange --mode=abm --tws --modulo=128 --count=200

# Single TEST command/response cycle
./test_exchange --test

# Ten TEST command/response cycles with a 64-byte information field
./test_exchange --test=10 --size=64

# TEST command mode with only endpoint A local
./test_exchange --test=100 --endpoint=a --size=32

# TEST responder-only mode with only endpoint B local
./test_exchange --test --endpoint=b --size=32
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
Progress: 5000/10000 packets sent, 4998 rcv | A: 2500/2500 | B: 2500/2498
```

**Time-based:**
```
Elapsed: 30/60 seconds | A: 500 sent, 498 rcv | B: 498 sent, 500 rcv
```

**Infinite:**
```
Elapsed: 120 seconds | A: 1000 sent, 998 rcv | B: 995 sent, 1000 rcv
```

### Final Report

At completion, the tool prints per-endpoint, per-peer and per-direction
statistics. When `--endpoint=a` or `--endpoint=b` is used, only the local
endpoint statistics are printed.

In NRM reports, endpoint A corresponds to the primary side and endpoint B to
the secondary side.

```
Endpoint A:
  Packets sent:     1000
  Packets received: 1000
  Seq errors:       0
  Bytes sent:       0000064000
  Bytes received:   0000064000

Endpoint B:
  Packets sent:     1000
  Packets received: 1000
  Seq errors:       0
  Bytes sent:       0000064000
  Bytes received:   0000064000

A -> B Traffic:
  Sent:       1000 packets (0000064000 bytes)
  Received:   1000 packets (0000064000 bytes)
  Lost:       0 packets (0.00%)
  Throughput: 64000.00 bytes/s (62.50 KB/s)

B -> A Traffic:
  Sent:       1000 packets (0000064000 bytes)
  Received:   1000 packets (0000064000 bytes)
  Lost:       0 packets (0.00%)
  Throughput: 64000.00 bytes/s (62.50 KB/s)
```

### Protocol Statistics

When compiled with `-DIOHDLC_ENABLE_STATISTICS` (default on Linux), additional per-peer counters are reported:

```
Protocol Statistics (A -> B peer):
  REJ received:     0
  Checkpoints:      0
  Timeouts:         0
  Out of sequence:  0
  Pool low water:   0
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

The tool creates up to 4 worker threads per test run:

| Thread | Role | Active when |
|--------|------|-------------|
| `pri_writer` | Endpoint A sends packets | `both` or `pri2sec` |
| `pri_reader` | Endpoint A receives packets | `both` or `sec2pri` |
| `sec_writer` | Endpoint B sends packets | `both` or `sec2pri` |
| `sec_reader` | Endpoint B receives packets | `both` or `pri2sec` |

Threads not needed for the selected direction exit immediately. Statistics are protected by per-endpoint mutexes.

When `--endpoint=a` or `--endpoint=b` is selected, only the local endpoint is
instantiated and only that endpoint's writer/reader workers are created.

The global flag `test_running_global` coordinates shutdown: when any thread completes its work or encounters an error, all threads stop. On Linux, Ctrl-C sets `test_stop_requested` via signal handler.

## Adapter Support

| Adapter | Platform | Error Injection | Constraints |
|---------|----------|-----------------|-------------|
| Mock | Linux, ChibiOS | Yes (0-100%) | None |
| UART | ChibiOS | No | None |
| SPI | ChibiOS | No | TWA + NRM only (`ADAPTER_CONSTRAINT_TWA_ONLY \| ADAPTER_CONSTRAINT_NRM_ONLY`) |

The tool checks adapter constraints before starting. If a SPI adapter is
selected with TWS or ABM mode, the tool prints an error and exits. If the link
type or mode is omitted, constrained adapters select their required defaults.

Adapters may optionally implement a timing hook. Exchange calls it after
configuration parsing and before station initialization, passing the effective
reply timeout. The hook is adapter-local: it does not change protocol semantics.

## Platform-Specific Details

### Linux

Runtime CLI via `getopt_long`. All options listed above are available. Ctrl-C triggers graceful shutdown.

```bash
./tests/linux/build/bin/test_exchange --help
```

### ChibiOS Standalone

Compile-time configuration via Makefile defines. See [ChibiOS Standalone Build](../tests/chibios/README_EXCHANGE.md) for the full list of `TEST_*` defines, including `TEST_MODULO`.

### ChibiOS Shell

Interactive shell with runtime CLI. It accepts the same long options as Linux,
plus `a2b`/`b2a` direction aliases and the short forms `-p N`, `-w N`. See
[ChibiOS Shell](../tests/chibios/README_SHELL.md) for shell-specific details.

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

Build with `-DIOHDLC_ENABLE_STATISTICS` to get REJ, checkpoint, timeout, pool low, and out-of-sequence counters in the final report.
