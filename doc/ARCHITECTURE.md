# ioHdlc Architecture

## Overview

ioHdlc is designed as a **portable, OS-agnostic HDLC protocol stack** that can run on multiple platforms through an abstraction layer (OSAL). The architecture separates protocol logic from OS-specific implementations, enabling the same core code to run on Linux, ChibiOS, and other RTOS environments.

## High-Level Architecture

```
┌─────────────────────────────────────────────────────────┐
│                  Application Layer                       │
│            (ioHdlcWrite / ioHdlcRead API)               │
└─────────────────────────────────────────────────────────┘
                           │
┌─────────────────────────────────────────────────────────┐
│                    HDLC Core                             │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐ │
│  │   Station    │  │     Peer     │  │  State       │ │
│  │   Manager    │  │   Manager    │  │  Machine     │ │
│  └──────────────┘  └──────────────┘  └──────────────┘ │
│                                                          │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐ │
│  │  Sequencing  │  │   Window     │  │   Timer      │ │
│  │   & N(S)/N(R)│  │   Control    │  │   Manager    │ │
│  └──────────────┘  └──────────────┘  └──────────────┘ │
└─────────────────────────────────────────────────────────┘
                           │
┌─────────────────────────────────────────────────────────┐
│                   Frame Layer                            │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐ │
│  │  Frame Pool  │  │     FCS      │  │   Byte       │ │
│  │  Management  │  │  Calculation │  │  Stuffing    │ │
│  └──────────────┘  └──────────────┘  └──────────────┘ │
└─────────────────────────────────────────────────────────┘
                           │
┌─────────────────────────────────────────────────────────┐
│              OS Abstraction Layer (OSAL)                 │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐ │
│  │   Threads    │  │  Semaphores  │  │    Events    │ │
│  │   (Runner)   │  │   & Mutexes  │  │   & Timers   │ │
│  └──────────────┘  └──────────────┘  └──────────────┘ │
└─────────────────────────────────────────────────────────┘
                           │
┌─────────────────────────────────────────────────────────┐
│              Stream Driver Interface                     │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐ │
│  │  TX Queue    │  │  RX Queue    │  │   Physical   │ │
│  │  Management  │  │  Management  │  │     Port     │ │
│  └──────────────┘  └──────────────┘  └──────────────┘ │
└─────────────────────────────────────────────────────────┘
                           │
                    ┌──────┴──────┐
                    │   Hardware   │
                    │  UART / DMA  │
                    └──────────────┘
```

## Core Components

### 1. HDLC Core (`src/ioHdlc_core.c`)

**Responsibilities:**
- Protocol state machine (NDM, NRM, etc.)
- Frame sequencing (N(S), N(R) management)
- Window control and flow control
- Error recovery (REJ, checkpoint retransmission)
- Timer management integration

**Key Functions:**
- `ioHdlcOnRxFrame()` - Process received frames
- `ioHdlcOnLineIdle()` - Handle line idle notifications
- `ioHdlcTxEntry()` - TX thread entry point
- `ioHdlcRxEntry()` - RX thread entry point (if separate)

**Design:**
- OS-agnostic: uses only OSAL primitives
- Event-driven: responds to RX frames, timeouts, user writes
- Thread-safe: uses mutexes for peer state protection

### 2. Station Management (`include/ioHdlc.h`)

**Concept:**
A **station** represents one end of the HDLC link. It can be:
- **Primary** (initiates connections, sends commands)
- **Secondary** (responds to commands)

**Station contains:**
- Configuration (mode, address, timeouts)
- List of peers
- Frame pool
- Driver instance
- Event source for application notifications

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

### 4. Frame Pool (`include/ioHdlcframepool.h`)

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

### 5. Stream Driver Interface (`include/ioHdlcswdriver.h`)

**Purpose:**
Abstract interface to physical layer (UART, SPI, USB, etc.).

**Key Operations:**
```c
typedef struct {
  void (*tx_submit)(ioHdlcStreamPort *port, void *framep);
  void (*rx_submit)(ioHdlcStreamPort *port, void *buf, size_t len);
  void (*tx_callback)(void *framep, int status);
  void (*rx_callback)(void *buf, size_t len, int status);
} ioHdlcStreamCallbacks;
```

**Flow:**
1. Core calls `tx_submit()` with a frame
2. Driver transmits data via hardware
3. Driver calls `tx_callback()` when complete
4. Driver submits RX buffers via `rx_submit()`
5. Driver calls `rx_callback()` when data received

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

### 7. Runner (`os/<platform>/src/ioHdlc_runner.c`)

**Purpose:**
Platform-specific thread management and integration with core.

**Responsibilities:**
- Create TX/RX threads
- Map OS timers to core timer interface
- Map OS events to core event system
- Call core entry points (`ioHdlcTxEntry`, `ioHdlcRxEntry`)

**Linux Runner:**
- Uses `pthread_create()` for threads
- Uses `timer_create()` for POSIX timers
- Implements event system with condition variables

**ChibiOS Runner:**
- Uses `chThdCreateStatic()` for threads
- Uses `chVTSet()` for virtual timers
- Integrates with ChibiOS event system

## Data Flow

### TX Path (Application → Wire)

```
Application
    │
    ├─ ioHdlcWrite(peer, data, len)
    │
    v
Station (validate, enqueue)
    │
    ├─ Signal TX thread via event
    │
    v
TX Thread (ioHdlcTxEntry)
    │
    ├─ Take frame from pool
    ├─ Build HDLC frame (address, control, data, FCS)
    ├─ Apply byte stuffing
    │
    v
Stream Driver
    │
    ├─ tx_submit(frame)
    │
    v
Hardware UART/DMA
    │
    v
   Wire
```

### RX Path (Wire → Application)

```
   Wire
    │
    v
Hardware UART/DMA
    │
    ├─ DMA interrupt
    │
    v
Stream Driver
    │
    ├─ rx_callback(buf, len)
    │
    v
RX Thread (ioHdlcRxEntry) or inline
    │
    ├─ Remove byte stuffing
    ├─ Validate FCS
    ├─ Parse frame
    │
    v
Core (ioHdlcOnRxFrame)
    │
    ├─ Check N(S), update N(R)
    ├─ Handle U-frames (SNRM, UA, DISC)
    ├─ Handle S-frames (RR, RNR, REJ)
    ├─ Handle I-frames (data)
    ├─ Update window
    ├─ Signal application via events
    │
    v
Application
    │
    ├─ ioHdlcRead(peer, buf, len)
```

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
ioHdlcWrite(peer, data, len)
    └─> Broadcast EVT_TX_DATA
            └─> TX thread wakes up
                    └─> Transmits frame

// Timer expires
Timer callback()
    └─> Broadcast EVT_TIMER_EXPIRED
            └─> TX thread wakes up
                    └─> Handles timeout (retransmit)

// Frame received
rx_callback()
    └─> ioHdlcOnRxFrame()
            └─> Broadcast EVT_RX_DATA
                    └─> Application thread wakes up
                            └─> ioHdlcRead() returns data
```

## Memory Management

### Frame Allocation

```c
// During init
ioHdlcFrameMemPoolInit(&pool, arena, size, frame_size, align);

// During operation
frame = hdlcTakeFrame(pool);     // O(1), lock-free or mutex
hdlcReleaseFrame(pool, frame);   // O(1)
```

**No malloc/free in critical path:**
- Pre-allocated pool
- Fixed-size frames
- Bounded memory usage

### Reference Counting

Frames can be shared between:
- TX queue (pending transmission)
- Retransmit buffer (awaiting ACK)
- Application (user data)

```c
hdlcAddRef(pool, frame);      // refcount++
hdlcReleaseFrame(pool, frame); // refcount--, free if 0
```

## Portability Strategy

### Abstraction Layers

1. **OSAL** (`os/<platform>/`)
   - Platform-specific implementations
   - Same API across platforms

2. **Frame Pool** (`os/<platform>/src/ioHdlcfmempool.c`)
   - Platform-specific memory allocation
   - Same semantics across platforms

3. **Runner** (`os/<platform>/src/ioHdlc_runner.c`)
   - Platform-specific threading
   - Same integration points

### Core Portability

The core (`src/ioHdlc*.c`, `include/ioHdlc*.h`) is **100% OS-agnostic**:
- No `#ifdef` for platform selection
- Only uses OSAL types and macros
- Same binary code on all platforms

## Configuration

### Compile-Time Options

```c
// Frame pool size
#define IOHDLC_FRAME_POOL_SIZE 32

// Logging level
#define IOHDLC_LOG_LEVEL 1  // 0=OFF, 1=ERROR, 2=WARN, 3=INFO, 4=DEBUG

// Window size
#define IOHDLC_WINDOW_SIZE 7

// Optional functions
#define IOHDLC_OPT_REJ  0x01  // Enable REJ
#define IOHDLC_OPT_SST  0x04  // Enable Selective Reject
```

### Runtime Configuration

```c
iohdlc_station_config_t config = {
  .mode = IOHDLC_OM_NRM,         // Normal Response Mode
  .flags = IOHDLC_FLG_PRI,       // Primary station
  .log2mod = 3,                  // Modulo 8
  .addr = 0x01,                  // Station address
  .reply_timeout_ms = 1000,      // Reply timeout
  .optfuncs = optfuncs_array,    // Optional functions
  // ...
};
```

## Extension Points

### Adding New Platforms

1. Implement OSAL (`os/newplatform/include/ioHdlcosal.h`)
2. Implement frame pool (`os/newplatform/src/ioHdlcfmempool.c`)
3. Implement runner (`os/newplatform/src/ioHdlc_runner.c`)
4. Create build system (Makefile, CMakeLists.txt)
5. Write platform-specific tests

### Adding New Physical Layers

1. Implement stream driver callbacks:
   - `tx_submit()`
   - `rx_submit()`
   - `tx_callback()`
   - `rx_callback()`
2. Integrate with hardware (UART, SPI, USB, etc.)
3. Handle DMA or interrupt-driven I/O
4. Call core callbacks on completion

## Performance Considerations

### Zero-Copy Design

- Frames passed by pointer
- Reference counting avoids copies
- DMA-friendly (contiguous buffers)

### Lock-Free Operations (where possible)

- Frame pool can use atomic operations
- Event system uses OS primitives
- Minimizes contention

### Bounded Latency

- Pre-allocated frames (no malloc)
- Fixed-size queues
- Predictable timer handling

## Future Enhancements

- **ABM and ARM modes**: Extend the trasmission modes to Asynchronous balanced (P2P) and unbalanced modes.
- **Multi-point master**: Support for polling multiple secondaries
- **Modulo 128**: Extended sequence numbers for high-throughput links
- **Selective Reject (SREJ)**: Retransmit only specific frames
- **Link quality monitoring**: Track error rates, adjust timeouts
- **Dynamic window sizing**: Adapt to line conditions
