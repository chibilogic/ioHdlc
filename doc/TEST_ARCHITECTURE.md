# Test Architecture

## Design Philosophy

The ioHdlc test system mirrors the library's own portability model: **test logic written once, compiled for every platform**.

Guiding principles:

- **No `#ifdef` in test scenarios** -- all platform isolation flows through abstraction layers (OSAL, adapter vtable).
- **Zero dynamic allocation** -- tests use pre-allocated memory arenas, matching the embedded deployment model.
- **VMT-based dispatch** -- the adapter abstraction follows the same virtual method table pattern as `ioHdlcStreamPort`, keeping test infrastructure consistent with library design.

## Layered Structure

The test system is organized in layers, each depending only on those below it:

```
  7  Platform config        test_config_linux.c, test_config_chibios.c
  6  Platform runners        test_runner_*.c (Linux), main_tests.c (ChibiOS)
  5  Test scenarios          common/scenarios/*.c
  4  Mock infrastructure     mock_stream, mock_stream_adapter
  3  Adapter abstraction     adapter_interface.h, adapter_mock, adapter_uart, adapter_spi
  2  Test framework          test_framework.h (parametrized tests, statistics, packets)
  1  Assertion primitives    test_helpers.h (TEST_ASSERT, RUN_TEST, TEST_SUITE)
```

**Layer 1 -- Assertion Primitives** (`test_helpers.h`): Macros for assertions (`TEST_ASSERT`, `TEST_ASSERT_EQ`, `_GOTO` variants), test runners (`RUN_TEST`, `RUN_TEST_ADAPTER`), and suite reporting (`TEST_SUITE_START/END`). Pure C, no OS dependencies.

**Layer 2 -- Test Framework** (`test_framework.h`): Structures for parametrized testing: `test_config_t` (mode, duration, traffic direction, error rate), `test_statistics_t` (latency, throughput, loss), `test_packet_t` (sequenced packets for validation). Used by `test_exchange.c`.

**Layer 3 -- Adapter Abstraction** (`adapter_interface.h`): Decouples test scenarios from the stream backend. The `test_adapter_t` vtable provides `init`, `deinit`, `reset`, `get_port_a`, `get_port_b`, `configure_error_injection`, and a `constraints` bitmask that aliases `IOHDLC_PORT_CONSTR_*` from `ioHdlcStreamPort`. Primary constraint enforcement occurs inside the protocol core (at `ioHdlcStationInit` and `ioHdlcStationLinkUp`); test runners may additionally inspect `adapter->constraints` to skip scenarios incompatible with the physical backend. This allows the same scenario to run against `mock_adapter` (in-process loopback), `adapter_uart` (ChibiOS physical UART), or `adapter_spi` (ChibiOS SPI).

**Layer 4 -- Mock Infrastructure** (`mock_stream.h`, `mock_stream_adapter.h`): `mock_stream` provides bidirectional byte streams with circular buffers, loopback mode, peer connection, and error injection via filter callbacks. `mock_stream_adapter` bridges `mock_stream` to the `ioHdlcStreamPort` interface, managing an RX thread for asynchronous frame reception.

**Layers 5-7** are scenario code, platform runners, and platform-specific configuration. See [Writing New Tests](TESTING.md) for details.

## The Adapter Abstraction

The adapter layer exists so that **one scenario source file runs against any backend**:

| Adapter | Platform | Backend | Constraints |
|---------|----------|---------|-------------|
| `mock_adapter` | All | In-process mock stream | None |
| `adapter_uart` | ChibiOS | Physical UART loopback | None |
| `adapter_spi` | ChibiOS | Physical SPI | `ADAPTER_CONSTRAINT_TWA_ONLY \| ADAPTER_CONSTRAINT_NRM_ONLY` |

The `ADAPTER_CONSTRAINT_*` flags defined in `adapter_interface.h` are aliases of the `IOHDLC_PORT_CONSTR_*` flags on `ioHdlcStreamPort`. Backend implementations set `port.constraints` directly on their `ioHdlcStreamPort` instances; the protocol core validates these constraints during `ioHdlcStationInit` (TWA check) and `ioHdlcStationLinkUp` (NRM-only check). Test runners may additionally inspect `adapter->constraints` to skip scenarios that are incompatible with the physical backend.

The error injection interface (`configure_error_injection`) is optional -- hardware adapters set it to `NULL`.

## Runner / Scenario Separation

Test scenarios (`common/scenarios/`) do not contain `main()`. This is a deliberate design choice:

- **Linux**: each scenario gets its own runner (`test_runner_*.c`) producing an independent binary. This allows parallel execution, isolated failures, and selective re-runs.
- **ChibiOS**: a single `main_tests.c` calls all scenarios sequentially within one firmware image, since flashing is expensive.

Runners handle adapter init/deinit per test, wiring the platform-specific entry point to the portable scenario logic.

## Memory Management

All test memory is pre-allocated in `test_arenas.h`:

| Arena | Size | Purpose |
|-------|------|---------|
| `shared_arena_primary` | 8 KB | Primary station frame pool |
| `shared_arena_secondary` | 8 KB | Secondary station frame pool |
| `shared_arena_single` | 8 KB | Single-station tests (e.g. frame pool) |

Tests execute sequentially (never in parallel), so arenas are safely reused across tests. This model matches the library's no-malloc design and works identically on Linux and embedded targets.

## Threading Model

Each protocol test typically creates two HDLC stations, each with TX and RX threads (4 threads total). The mock stream synchronizes access using OSAL mutexes and condition variables.

Adapter `init()`/`deinit()` per test ensures clean state -- no cross-test contamination from leftover threads or buffered data.

## Platform Portability

| Component | Linux | ChibiOS |
|-----------|-------|---------|
| OSAL | POSIX pthreads | ChibiOS/RT threads |
| Mock stream | Circular buffer + pthread sync | Circular buffer + ChibiOS sync |
| Mock adapter | `adapter_mock.c` (shared) | `adapter_mock.c` (shared) |
| UART adapter | -- | `adapter_uart.c` |
| SPI adapter | -- | `adapter_spi.c` |
| Test scenarios | All from `common/scenarios/` | All from `common/scenarios/` |
| Platform tests | OSAL bsem/events, mock stream | Hardware-specific |
| Config/CLI | `test_config_linux.c` (getopt) | `test_config_chibios.c` (shell) |

## Testing Levels

| Level | Status | Description |
|-------|--------|-------------|
| 1: Linux Mock | Implemented | POSIX OSAL, in-memory mock stream. Fast iteration, CI-friendly. |
| 2a: ChibiOS Mock | Implemented | Real ChibiOS OSAL, mock stream on target. Validates RTOS integration. |
| 2b: ChibiOS + Hardware | Implemented | Physical UART loopback and SPI (master/slave, `IOHDLC_PORT_CONSTR_TWA_ONLY \| IOHDLC_PORT_CONSTR_NRM_ONLY`). Conditional build via `USE_UART_ADAPTER`/`USE_SPI_ADAPTER`. |
| 3: Core Unit Tests | Implemented | Isolated tests for frame pool (init, ref-counting, watermarks, exhaustion), OSAL primitives (binary semaphore, event system), and mock stream infrastructure. |

## Adding a New Platform

1. Implement `test_config_<platform>.c` -- configuration parsing for the platform.
2. Implement one or more adapters conforming to `test_adapter_t`.
3. Create runner entry points that link the common scenarios.
4. All scenario code and framework code are reused unchanged.

## References

- [Test Suite README](../tests/README.md) -- what tests exist, how to build and run, implemented/planned checklists
- [Testing Guide](TESTING.md) -- how to write new tests, framework API, debugging
