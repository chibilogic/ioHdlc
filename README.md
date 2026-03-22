# ioHdlc

**ioHdlc** is a portable HDLC protocol stack compliant with ISO 13239,
designed for embedded systems and real-time environments.

It targets applications that need a **reliable, high-throughput, deterministic**
data-link layer over UART, SPI, or similar byte-stream transports — without
dynamic memory allocation and without sacrificing a clean application interface.

---

## Key Features

### Protocol
- **ISO 13239 HDLC** compliance
- **NRM (Normal Response Mode)** — primary/secondary roles
- **TWS (Two-Way Simultaneous)** and **TWA (Two-Way Alternate)** link configurations
- Connection management: SNRM/UA/DISC/DM with configurable retry and timeout
- I-frame data transfer with **sliding-window flow control** (modulo 8/128)
- **REJ** and **checkpoint retransmission** for error recovery
- **Frame Format Field (FFF)**: ISO 13239 optional extension that encodes the
  frame length in the header, enabling single-shot DMA reception of the entire
  frame and eliminating the need for end-flag byte scanning

### Application Interface
- **Byte-stream read/write API** (`ioHdlcReadTmo` / `ioHdlcWriteTmo`): the
  application reads and writes raw bytes with POSIX-style blocking and timeout
  semantics — I-frame fragmentation and reassembly are transparent.
- **Integrated backpressure**: writes block not only on a full sliding window
  but also when the frame pool reaches its low watermark, propagating memory
  pressure to the application automatically.
- **Link event notifications**: asynchronous flags (`LINK_UP`, `LINK_REFUSED`,
  `LINK_LOST`, `LINK_TIMEOUT`) let the application react to link state changes
  without polling.
- **Multi-peer support**: a single primary station manages multiple secondary
  peers simultaneously (multipoint configuration).

### Memory and Performance
- **No dynamic allocation in the critical path**: frames are managed through
  a pre-allocated pool supplied by the application at init time.
- **Zero-copy frame passing**: frames move by pointer through all layers;
  reference counting enables DMA-friendly ownership transfers.
- **Bounded memory footprint**: pool size and frame arena are fixed and
  fully known at integration time.

### Architecture
- **OS-agnostic core**: protocol logic and runner use only OSAL primitives;
  the same source builds on ChibiOS and other targets without modification
  (Linux is supported as a protocol test environment).
- **Pluggable transport**: the stream-port abstraction (`ioHdlcStreamPort`)
  decouples HDLC framing from the byte-stream backend — UART, SPI, or custom
  adapters connect through the same interface.
- **VMT-based extensibility**: drivers and frame pools implement a virtual
  method table; capabilities (FCS size, transparency, FFF) are negotiated at
  init time, allowing custom implementations without modifying the core.

Dual-licensed under **GPL-3.0** and a **commercial license** — see
[Licensing](#licensing) below.

---

## Supported Platforms

| Platform    | Role                                       |
|:------------|:-------------------------------------------|
| ChibiOS/RT  | Production target (UART, SPI)              |
| Linux/POSIX | Protocol verification and test environment |
| Other RTOS  | Portable via OSAL and stream-port adapter  |

Porting to a new platform requires implementing only the OSAL primitives and a
stream-port adapter — the protocol core and runner require no modifications.

---

## Use Cases

- **Point-to-point serial links** — UART or RS-485 between an MCU and a
  peripheral, or between two boards. The sliding window provides flow control
  and error recovery over a single physical link without the overhead of a
  full network stack.
- **Industrial protocols and fieldbus** — HDLC serves as the data-link layer
  for standards such as IEC 62056 (smart metering) and PROFIBUS-like profiles.
  ioHdlc provides framing, sequencing, and retransmission; the application
  protocol builds on top.
- **Inter-processor communication** — SPI or UART between a main processor and
  a coprocessor (modem, sensor hub, radio) where reliable framing is needed
  without TCP/IP. TWA mode fits half-duplex transports naturally.
- **Telemetry and remote links** — Radio, satellite, or powerline channels
  with limited bandwidth and high error rates. The sliding window and
  checkpoint retransmission are designed for lossy, high-latency links.
- **Secure board-to-board links** — HDLC provides the reliable, ordered byte
  stream that TLS requires, enabling encrypted communication between boards
  (e.g. over SPI at >10 Mb/s) without a full TCP/IP stack.

---

## Quick Start

A minimal primary station setup:

```c
/* 1. Declare objects */
static ioHdlcSwDriver        driver;
static iohdlc_station_t      station;
static iohdlc_station_peer_t peer;
static uint8_t               arena[IOHDLC_ARENA_SIZE]; /* depends on frame count and size */

/* 2. Init driver and set up transport (platform-specific) */
ioHdlcSwDriverInit(&driver);
ioHdlcStreamPort port;          /* init your UART/SPI adapter here */

/* 3. Init station — phydriver must be set before init */
iohdlc_station_config_t cfg = {
    .mode             = IOHDLC_OM_NRM,
    .flags            = IOHDLC_FLG_PRI,
    .addr             = 0x01,
    .log2mod          = 3,          /* modulo 8 */
    .driver           = (ioHdlcDriver *)&driver,
    .phydriver        = &port,
    .frame_arena      = arena,
    .frame_arena_size = sizeof arena,
    .reply_timeout_ms = 1000,
};
ioHdlcStationInit(&station, &cfg);

/* 4. Add peer and start runner */
ioHdlcAddPeer(&station, &peer, 0x02);
ioHdlcRunnerStart(&station);

/* 5. Connect, exchange data */
ioHdlcStationLinkUp(&station, 0x02, IOHDLC_OM_NRM);

uint8_t tx_buf[] = "hello";
ioHdlcWriteTmo(&peer, tx_buf, sizeof tx_buf, IOHDLC_WAIT_FOREVER);

uint8_t rx_buf[64];
ssize_t n = ioHdlcReadTmo(&peer, rx_buf, sizeof rx_buf, 5000);

/* 6. Disconnect and stop */
ioHdlcStationLinkDown(&station, 0x02);
ioHdlcRunnerStop(&station);
```

---

## Building and Testing

See the [Testing Guide](doc/TESTING.md) for full details and the
[Hardware Getting Started](doc/GETTING_STARTED_HARDWARE.md) guide for running
on a Nucleo-F411RE board. Linux test suite quick start:

```bash
make -C tests/linux test        # build and run all automated tests
make -C tests/linux test-osal   # OSAL tests only
```

---

## Documentation

- [Architecture Overview](doc/ARCHITECTURE.md)
- [HDLC Protocol Details](doc/PROTOCOL.md)
- [Testing Guide](doc/TESTING.md)
- [Test Architecture](doc/TEST_ARCHITECTURE.md)

---

## Licensing

ioHdlc is **dual-licensed**. Choose the license that fits your use case:

### Open Source License

ioHdlc is released under the
**GNU General Public License v3.0 or later (GPL-3.0-or-later)**.

This license is suitable for open-source projects and allows free use,
modification, and distribution provided that:
- the complete source code of any work that uses ioHdlc is made available
  under the same GPL license;
- the original copyright notice and license text are retained.

See the `LICENSE` file for the full license text.

### Commercial License

**Products that cannot release their source code under the GPL require a
commercial license.** This is the typical case for proprietary embedded
firmware and commercial products.

Commercial licenses are granted and managed by **Chibilogic s.r.l.**

Contact: info@chibilogic.com

---

## Copyright

Copyright (C) 2024–2026 **Isidoro Orabona** — All rights reserved.

---

## Contributing

Contributions are welcome. Unless explicitly stated otherwise, any contribution
submitted for inclusion in this repository is assumed to be provided under the
terms of the GNU General Public License v3.0 or later.

---

## Disclaimer

This software is provided *as is*, without warranty of any kind,
express or implied. See the LICENSE file for details.

