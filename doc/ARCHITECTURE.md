# Architecture

## Overview

ioHdlc is designed as a **portable, OS-agnostic HDLC protocol stack** that can run on multiple platforms through an abstraction layer (OSAL). The architecture separates protocol logic from OS-specific implementations, enabling the same core code to run on ChibiOS, Linux (test platform), and other RTOS environments.

The current implementation is centered on the station/peer model, the shared runner, the software framed driver layered on top of a byte-stream backend, and the connection-management and data-transfer paths used by the currently operational modes. The architecture also reserves space for additional HDLC modes such as ABM and ARM, but those paths are not yet complete.

## High-Level Architecture

![Architecture module relationships](diagrams/svg/architecture_modules.svg)

## Core Components

### 1. HDLC Core

**Source:** `src/ioHdlc_core.c`

**Responsibilities:**
- Protocol state machine for the currently implemented connection-management and data-transfer paths
- Frame sequencing (N(S), N(R) management)
- Window control and flow control
- Error recovery (REJ, checkpoint retransmission)
- Timer management integration

**Key Functions:**
- `ioHdlcOnRxFrame()` - Process received frames
- `ioHdlcOnLineIdle()` - Handle line idle notifications and return runner event flags
- `ioHdlcTxEntry()` - TX thread entry point
- `ioHdlcRxEntry()` - RX thread entry point (if separate)

**Design:**
- OS-agnostic: uses only OSAL primitives
- Event-driven: responds to RX frames, timeouts, user writes
- Thread-safe: uses mutexes for peer state protection

### 2. Station Management

**Source:** `include/ioHdlc.h`, `include/ioHdlctypes.h`

**Note:** `ioHdlc.h` is the umbrella public header — it aggregates all public includes. The station type (`iohdlc_station_t`) and peer type (`iohdlc_station_peer_t`) are defined in `include/ioHdlctypes.h`.

**Concept:**
A **station** represents one end of the HDLC link. It can be:
- **Primary** (initiates connections, sends commands)
- **Secondary** (responds to commands)

**Station contains:**
- Configuration (mode, address, timeouts)
- List of peers
- Frame pool
- Driver instance
- Internal event source (`cm_es`) for protocol event synchronisation between threads
- Application event source (`app_es`) for notifying upper layers

### 3. Peer Management

**Concept:**
A **peer** represents a remote station that this station communicates with.

**Peer contains:**
- Remote address
- Connection state (disconnected, connecting, connected)
- TX/RX sequence numbers (V(S), V(R), N(R))
- Send window management
- Retry counters
- Timer state

**Multi-peer support:**
One station can communicate with multiple peers (multi-point configuration).

### 4. Frame Pool

**Source:** `include/ioHdlcframepool.h`

**Purpose:**
Pre-allocated frame buffers to avoid dynamic memory allocation in real-time paths.

**Features:**
- **Reference counting**: frames can be shared
- **Watermark monitoring**: LOW/NORMAL thresholds
- **Thread-safe**: lock-free or mutex-protected operations
- **Platform-specific**: implemented in `os/<platform>/src/ioHdlcfmempool.c`

**Operations:**
```c
iohdlc_frame_t *frame = hdlcTakeFrame(pool);  // Allocate
hdlcAddRef(pool, frame);                       // Increment refcount
hdlcReleaseFrame(pool, frame);                 // Decrement, free if 0
```

### 5. Stream Driver and Stream Port Boundary

**Source:** `include/ioHdlcswdriver.h`, `include/ioHdlcstreamport.h`

**Purpose:**
Separate HDLC framing logic from the byte-stream backend used to move data on a transport such as UART, SPI, or a mock adapter.

**Note:** The structs below are simplified for illustration; see `include/ioHdlcstreamport.h` for the authoritative definitions.

**Architecture:**
```c
typedef struct ioHdlcStreamPortOps {
  void (*start)(void *ctx, const ioHdlcStreamCallbacks *cbs);
  void (*stop)(void *ctx);
  bool (*tx_submit)(void *ctx, const uint8_t *ptr, size_t len, void *framep);
  bool (*tx_busy)(void *ctx);
  bool (*rx_submit)(void *ctx, uint8_t *ptr, size_t len);
  void (*rx_cancel)(void *ctx);
} ioHdlcStreamPortOps;

typedef struct ioHdlcStreamCallbacks {
  ioHdlcStreamOnRx      on_rx;
  ioHdlcStreamOnTxDone  on_tx_done;
  ioHdlcStreamOnRxError on_rx_error;
  void                 *cb_ctx;
} ioHdlcStreamCallbacks;
```

**`ioHdlcStreamPortOps` operations:**

| Operation | Direction | Description |
|---|---|---|
| `start` | Driver → Backend | Bind the driver callback bundle; begin transport activity |
| `stop` | Driver → Backend | Shutdown the transport; release backend-owned resources |
| `tx_submit` | Driver → Backend | Submit a TX buffer (non-blocking); returns `false` if the transport is busy |
| `tx_busy` | Driver → Backend | Query whether a TX transfer is currently in progress |
| `rx_submit` | Driver → Backend | Arm a receive transfer of `len` bytes into `ptr` |
| `rx_cancel` | Driver → Backend | Cancel the currently armed RX transfer |

All operations are required; backends must implement the full table.

**Responsibilities:**
- `ioHdlcSwDriver` implements the framed HDLC driver on top of a stream port.
- `ioHdlcStreamPortOps` are provided by the backend and called by the driver.
- `ioHdlcStreamCallbacks` are registered by the driver and invoked by the backend.
- The stream layer moves bytes and transport events only; HDLC framing, FCS handling, transparency, and frame ownership remain in the driver/core layers.

**Flow direction:**
1. The driver starts the backend and registers its callback bundle.
2. The driver submits RX/TX work through `ioHdlcStreamPortOps`.
3. The backend reports RX progress, TX completion, and transport errors through `ioHdlcStreamCallbacks`.
4. The software driver turns those byte-stream events into frame-oriented interactions for the core.

The callback-oriented interaction is captured in the following sequence diagram.

![Stream callback flow](diagrams/svg/stream_callback_flow.svg)

### 6. OS Abstraction Layer (OSAL)

**Purpose:**
Provide uniform OS primitives across platforms.

**Abstracted Primitives:**
- **Threads**: `iohdlc_thread_t`, thread creation/join
- **Mutexes**: `iohdlc_mutex_t`, lock/unlock
- **Semaphores**: `iohdlc_sem_t`, wait/signal
- **Binary Semaphores**: `iohdlc_binary_semaphore_t`
- **Events**: `iohdlc_event_source_t`, broadcast/wait
- **Timers**: `iohdlc_virtual_timer_t`, start/stop/expired

**Implementations:**
- `os/linux/`: POSIX threads, pthread mutexes, condition variables
- `os/chibios/`: ChibiOS threads, mutexes, semaphores, virtual timers

### 7. Runner

**Source:** `src/ioHdlc_runner.c`

**Purpose:**
Provide the common execution shell around the station core.

**Responsibilities:**
- Create TX/RX threads
- Map OS timers to core timer interface
- Map OS events to core event system
- Call core entry points (`ioHdlcTxEntry`, `ioHdlcRxEntry`)

**Design:**
- The runner logic is shared and lives in the common source tree.
- Thread creation, timers, events, and synchronization are delegated to OSAL.
- Platform-specific behaviour lives below the runner, in OSAL and backend implementations.

#### 7.1 Runner → Core: entry points

The runner delivers execution signals to the core through entry points declared in `include/ioHdlc_core.h`:

| Function | Description |
|---|---|
| `ioHdlcOnRxFrame(station, fp)` | Hand a fully received frame to the core for protocol processing |
| `ioHdlcOnLineIdle(station)` | Notify the core of an inter-frame idle condition on the transport |

These are called from runner-owned execution contexts (threads or ISR deferral).

#### 7.2 Core → Runner: concrete link-time functions

The core is OS-agnostic but calls concrete functions defined in `src/ioHdlc_runner.c`, resolved at link time. These functions bridge the core's protocol logic to the runner's OSAL-based timer and event infrastructure without runtime indirection.

**Timer management (per peer):**

| Function | Description |
|---|---|
| `ioHdlcStartReplyTimer(peer, kind, ms)` | Arm a reply timer for the given peer and timer kind |
| `ioHdlcRestartReplyTimer(peer, kind, ms)` | Restart a reply timer only if it is currently armed |
| `ioHdlcStopReplyTimer(peer, kind)` | Disarm the timer and clear its expired flag |
| `ioHdlcIsReplyTimerExpired(peer, kind)` | Return `true` if the timer fired and was not explicitly stopped |

**Event management (macros defined in `include/ioHdlc_core.h`):**

| Macro / Function | Description |
|---|---|
| `ioHdlcBroadcastFlags(station, flags)` | Broadcast internal core event flags to the station's `cm_es` |
| `ioHdlcBroadcastFlagsApp(station, flags)` | Broadcast application-facing event flags to `app_es` |
| `ioHdlcWaitEvents(station)` | Block until any core event flag is set; return and clear the pending mask |

## Data Flow

### TX Path (Application → Wire)

Application calls `ioHdlcWriteTmo()` → data is enqueued and `IOHDLC_EVT_TX_IFRM_ENQ` is broadcast → the TX thread wakes, builds an I-frame → the driver serializes it (FCS, transparency encoding) → the stream port transmits the bytes over the physical transport.

![TX data flow](diagrams/svg/tx_data_flow.svg)

### RX Path (Wire → Application)

The stream port receives bytes → the driver assembles a frame and validates the FCS → `ioHdlcOnRxFrame()` delivers the frame to the core → the core processes the control fields and broadcasts `IOHDLC_EVT_I_RECVD` → the application's `ioHdlcReadTmo()` call returns the data.

![RX data flow](diagrams/svg/rx_data_flow.svg)

## Integration Lifecycle

The lifecycle below focuses on the normal integration path from init to
shutdown.

**Note:** `ioHdlcStationInit()` automatically starts the driver when `config->phydriver` is set, so the transport adapter must be fully initialized before calling init.

![Integration lifecycle](diagrams/svg/integration_lifecycle.svg)

## Threading Model

### Two-Thread Architecture

**TX Thread:**
- Waits for events (data to send, timer expiry)
- Builds and transmits frames
- Manages retransmissions

**RX Thread/Callback:**
- Processes received frames
- Updates state machine
- Signals TX thread as needed

**Why Two Threads?**
- **Decoupling**: TX and RX operate independently
- **Responsiveness**: RX can process frames immediately
- **Simplicity**: Clear separation of concerns

### Event-Driven Synchronization

```c
// Application writes data
ioHdlcWriteTmo(peer, data, len, tmo)
    └─> Broadcast IOHDLC_EVT_TX_IFRM_ENQ
            └─> TX thread wakes up
                    └─> Transmits frame

// Timer expires
Timer callback()
    └─> Broadcast IOHDLC_EVT_C_RPLYTMO
            └─> TX thread wakes up
                    └─> Handles timeout (retransmit)

// Frame received
rx_callback()
    └─> ioHdlcOnRxFrame()
            └─> Broadcast IOHDLC_EVT_I_RECVD
                    └─> Application thread wakes up
                            └─> ioHdlcReadTmo() returns data
```

## Memory Management

All memory is pre-allocated at init time — no `malloc`/`free` occurs in the critical path. Frame buffers are fixed-size and drawn from the pool described in section 4. Reference counting (`hdlcTakeFrame`, `hdlcAddRef`, `hdlcReleaseFrame`) allows a single frame to be shared across the TX queue, the retransmit buffer, and the application layer without copying. Memory usage is bounded by the arena size provided at init.

The frame lifecycle below summarizes the ownership handoff between pool,
protocol queues, driver usage, and final recycle.

![Frame lifecycle](diagrams/svg/frame_lifecycle.svg)

## Portability Strategy

### Abstraction Layers

1. **OSAL** (`os/<platform>/`)
   - Platform-specific implementations
   - Same API across platforms

2. **Stream backends / adapters**
    - Map a transport implementation to `ioHdlcStreamPort`
    - Define callback context, DMA constraints, and buffer ownership at the transport boundary

3. **Frame pool backend** (`os/<platform>/src/ioHdlcfmempool.c`)
    - Platform-specific allocation and synchronization details
    - Same pool/refcount/watermark semantics across platforms

4. **Common runner and core** (`src/`)
    - Shared execution model and protocol logic
    - Platform-independent as long as OSAL and the selected backend honour the contracts

### Core Portability

The core and runner (`src/ioHdlc*.c`, `include/ioHdlc*.h`) are designed to stay OS-agnostic:
- No `#ifdef` for platform selection
- Only uses OSAL types and macros
- Same integration model across supported platforms

## Configuration

### Runtime Configuration Model

```c
iohdlc_station_config_t config = {
  .mode = IOHDLC_OM_NRM,
  .flags = IOHDLC_FLG_PRI,
  .log2mod = 3,
  .addr = 0x01,
  .driver = (ioHdlcDriver *)&driver,
  .frame_arena = arena,
  .frame_arena_size = sizeof arena,
  .max_info_len = 0,          // auto from driver / FFF constraints
  .pool_watermark = 0,        // auto
  .fff_type = 0,              // auto
  .optfuncs = optfuncs_array,
  .reply_timeout_ms = 1000,
  .poll_retry_max = 0         // default policy
};
```

**Station initialization derives additional runtime state:**
- frame size and pool dimension from the arena and driver constraints
- control field size and frame offset from the selected modulo / FFF policy
- fast-access protocol flags from the optional-functions bitmap
- final driver configuration after validating FCS, transparency, and FFF compatibility against driver capabilities

## Extension Points

### Adding New Platforms

1. Implement OSAL (`os/newplatform/include/ioHdlcosal.h`)
2. Implement frame pool (`os/newplatform/src/ioHdlcfmempool.c`)
3. Implement or adapt a stream backend / transport adapter
4. Reuse the common runner and core
5. Create build system (Makefile, CMakeLists.txt)
6. Write platform-specific tests

### Adding New Physical Layers

1. Implement stream driver callbacks:
    - backend ops such as `start()`, `tx_submit()`, `rx_submit()`
    - callback notifications such as `on_rx()`, `on_tx_done()`, `on_rx_error()`
2. Integrate with hardware (UART, SPI, USB, etc.)
3. Handle DMA or interrupt-driven I/O
4. Respect the ownership and callback-context contract documented by the backend

## Performance Considerations

The design decisions described above — pre-allocated frame pools, reference-counted zero-copy frame passing, contiguous DMA-friendly buffers, and OSAL-level lock-free or mutex-protected operations — combine to provide bounded latency and predictable memory usage suitable for real-time embedded systems.

## Future Enhancements

- **ABM and ARM modes**: Complete the currently reserved ABM/ARM paths. Mode constants and placeholder handlers already exist, but the transmit-side logic is not finished yet.
- **Multi-point master**: Support for polling multiple secondaries
- **Selective Reject (SREJ)**: Retransmit only specific frames
- **Link quality monitoring**: Track error rates, adjust timeouts
- **Dynamic window sizing**: Adapt to line conditions

---

## Appendix A: VMT Polymorphism Pattern

ioHdlc uses a C polymorphism pattern. It provides runtime dispatch without C++ overhead, using only plain structs and function pointers.

### A.1 Structure of an interface

Each abstract interface declares three building blocks in its header:

**`_methods` macro** — the vtable layout: one function pointer entry per virtual operation.

**`_data` macro** — data fields shared by all implementations of the interface.

**Base struct** — combines a `vmt` pointer (must be the **first field**) with the `_data` macro fields. Because `vmt` is first, a pointer to any concrete type can be safely cast to the base.

```c
/* From include/ioHdlcdriver.h */

#define _iohdlc_driver_methods                                       \
  void (*start)(void *ip, void *phydrvp, void *phyconfigp,           \
      ioHdlcFramePool *fpp);                                         \
  void (*stop)(void *ip);                                             \
  size_t (*send_frame)(void *ip, iohdlc_frame_t *fp);                \
  iohdlc_frame_t * (*recv_frame)(void *ip, iohdlc_timeout_t tmo);    \
  const ioHdlcDriverCapabilities* (*get_capabilities)(void *ip);      \
  int32_t (*configure)(void *ip, uint8_t fcs_size,                   \
      bool transparency, uint8_t fff_type);

#define _iohdlc_driver_data  \
  ioHdlcFramePool *fpp;

struct _iohdlc_driver_vmt { _iohdlc_driver_methods };

typedef struct {
  const struct _iohdlc_driver_vmt *vmt;   /* MUST be first field */
  _iohdlc_driver_data
} ioHdlcDriver;
```

### A.2 Concrete implementation

A concrete type embeds the base struct as its **first field**, then adds its own state. It provides a `static const` vtable and assigns it in its init function:

```c
/* Concrete type — from src/ioHdlcswdriver.c */
typedef struct {
  ioHdlcDriver      base;    /* MUST be first */
  ioHdlcStreamPort *port;
  /* ... other implementation-specific fields ... */
} ioHdlcSwDriver;

/* One vtable per type, shared by all instances */
static const struct _iohdlc_driver_vmt swdrv_vmt = {
  .start            = swdrv_start,
  .stop             = swdrv_stop,
  .send_frame       = swdrv_send_frame,
  .recv_frame       = swdrv_recv_frame,
  .get_capabilities = swdrv_get_capabilities,
  .configure        = swdrv_configure,
};

void ioHdlcSwDriverInit(ioHdlcSwDriver *drv, ...) {
  drv->base.vmt = &swdrv_vmt;   /* bind vtable */
  /* initialise remaining fields */
}
```

### A.3 Dispatch at call sites

Callers hold a pointer to the base type and dispatch through the vtable, either directly or via a dispatch macro defined alongside the interface:

```c
ioHdlcDriver *drv = station->driver;

/* Direct dispatch */
drv->vmt->send_frame(drv, fp);

/* Via dispatch macro */
hdlcSendFrame(drv, fp);
```

### A.4 Interfaces using this pattern

| Interface | Base type | Concrete implementation | Source |
|---|---|---|---|
| Framed driver | `ioHdlcDriver` | `ioHdlcSwDriver` | `src/ioHdlcswdriver.c` |
| Frame pool | `ioHdlcFramePool` | `ioHdlcFrameMemPool` | `os/<platform>/src/ioHdlcfmempool.c` |

To add a new implementation: embed the base struct as the first field of your concrete struct, fill a `static const` vtable with your function pointers, and assign the vtable in your init. No changes to the core or runner are needed.
