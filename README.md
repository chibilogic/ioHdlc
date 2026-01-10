# ioHdlc

ioHdlc is a reusable and configurable HDLC core/library intended for
digital communication systems and embedded or FPGA-based designs.

The project is designed to be easily integrable in both open-source
and proprietary systems, while maintaining a clear and robust
licensing model.

---

## Features

### Protocol

- **ISO 13239 HDLC** compliance
- **NRM (Normal Response Mode)** and **NDM (Normal Disconnected Mode)**
- **TWS (Two-Way Simultaneous)** and **TWA (Two-Way Alternate)** operation mode
- Configurable **window sizes** (modulo 8/128)
- **REJ (Reject)** and **checkpoint retransmission** recovery
- Frame sequence number management with wraparound

### Architecture

- **OS-abstraction layer** (OSAL) for portability
- **Frame pool management** with reference counting
- **Thread-safe** operations
- **Stream-based driver interface** for various physical layers
- **Event-driven** state machine

### Platforms

- **Linux/POSIX** (pthread-based)
- **ChibiOS/RT** (ARM Cortex, extensible to other targets)
- Easily portable to other RTOS/bare-metal environments

---

## Documentation

- [Architecture Overview](doc/ARCHITECTURE.md)
- [HDLC Protocol Details](doc/PROTOCOL.md)
- [Testing Guide](doc/TESTING.md)
- [Test Architecture](doc/TEST_ARCHITECTURE.md)
- [Debug Guide](doc/DEBUG_GUIDE.md)

---

## Licensing

ioHdlc is **dual-licensed**.

### Open Source License

ioHdlc is released under the  
**GNU Lesser General Public License v3.0 or later (LGPL-3.0-or-later)**.

You are free to:
- use the software in open-source or proprietary projects
- link it with proprietary applications

Provided that:
- the ioHdlc code itself remains under the LGPL
- any modifications to ioHdlc are made available under the same license

See the `LICENSE` file for the full license text.

---

### Commercial License

ioHdlc is also available under a **commercial license**.

Commercial licenses are **granted and managed by Chibilogic s.r.l.**
under authorization from the copyright holder.

A commercial license allows use of ioHdlc without the obligations
imposed by the LGPL (e.g. disclosure of modifications, relinking
requirements, or other copyleft conditions), according to the terms
defined in the commercial agreement.

For commercial licensing inquiries, please contact:

**Chibilogic s.r.l.**  
📧 info@chibilogic.com

---

## Copyright

Copyright (C) 2024  
**Isidoro Orabona**

All rights reserved.

---

## Contributing

Contributions are welcome.

Unless explicitly stated otherwise, any contribution submitted for
inclusion in this repository is assumed to be provided under the
terms of the GNU Lesser General Public License v3.0 or later.

---

## Disclaimer

This software is provided *as is*, without warranty of any kind,
express or implied. See the LICENSE file for details.

