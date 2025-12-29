# ioHdlc Test Suite

Test infrastructure per ioHdlc organizzata in tre livelli di testing.

## Struttura

```
tests/
├── common/                     # Shared across all platforms
│   ├── test_helpers.h/c        # Test framework
│   └── scenarios/              # OS-agnostic test scenarios
│       ├── test_frame_pool.c       # Frame pool tests (portable)
│       └── test_basic_connection.c # HDLC core tests (portable)
│
├── linux/                      # Linux/POSIX implementation
│   ├── mocks/                  # Mock implementations
│   │   ├── mock_stream.h       # Mock stream (simulates UART)
│   │   └── mock_stream.c       # Buffer-based stream with loopback
│   │
│   ├── scenarios/              # Linux-specific test scenarios
│   │   └── test_mock_stream.c  # Mock validation
│   │
│   └── Makefile                # Builds common + linux tests
│
└── chibios/                    # ChibiOS (future)
    └── Makefile                # Builds common + chibios tests

os/
├── linux/                      # OSAL for Linux/POSIX
│   ├── include/
│   │   └── ioHdlcosal.h        # OSAL header (pthread, semaphores)
│   └── src/
│       ├── ioHdlcosal.c        # OSAL implementation
│       └── ioHdlcfmempool.c    # Frame pool implementation
│
└── chibios/                    # OSAL for ChibiOS
    ├── include/
    │   └── ioHdlcosal.h
    └── src/
        ├── ioHdlcosal.c
        └── ioHdlcfmempool.c
```

## Filosofia di Testing

### Test OS-Agnostic (`common/scenarios/`)
Test che usano solo interfacce astratte, portabili su qualsiasi OS:
- **Frame Pool**: Usa solo `hdlcTakeFrame`, `hdlcReleaseFrame`, `hdlcAddRef`
- **HDLC Core**: Testa stazioni, handshake, gestione connessioni
- **Stream Driver**: Testa interfaccia `ioHdlcStream` astratta

Questi test sono compilati per ogni piattaforma target ma condividono lo stesso sorgente.

### Test Platform-Specific
Test specifici per l'implementazione OS:
- **Linux**: Validazione mock stream, verifica OSAL
- **ChibiOS**: Test con hardware reale (futuro)

## Livelli di Testing

### Livello 1: Linux Mock (Attuale)
**Status**: ✅ Implementato
- OSAL POSIX con pthread/semaphore/mutex
- Mock stream in memoria con buffer circolari
- Frame pool con free-list thread-safe
- Test scenarios senza hardware

**Vantaggi**:
- Sviluppo rapido senza dipendenze HW
- Debugging semplice (gdb, valgrind)
- Esecuzione in CI/CD

### Livello 2a: ChibiOS Mock (TODO)
**Status**: ⏳ Da implementare
- ChibiOS OSAL reale (threads, semaphores)
- Stream mock che simula UART
- Test su target embedded senza HW

### Livello 2b: ChibiOS + HW (TODO)
**Status**: ⏳ Da implementare
- Due UART sulla stessa board
- Loopback fisico (TX1→RX2, TX2→RX1)
- Test con timing reale

**Note**: Permette di validare il protocollo su linea reale usando una sola board.

### Livello 3: Unit Tests Core (TODO)
**Status**: ⏳ Da implementare
- Test isolati della logica core
- Mock minimali per stream/OSAL
- Focus su: frame parsing, sequence numbers, window logic

## Build e Esecuzione

### Linux Tests

```bash
cd tests/linux

# Build tutti i test
make

# Build e esegui tutti i test
make test

# Pulisci artifacts
make clean
```

### Test Individuali

```bash
# Esegui solo test di connessione
./build/bin/test_basic_connection
```

## Debugging

### Con GDB
```bash
gdb ./build/bin/test_basic_connection
(gdb) run
(gdb) bt
```

### Con Valgrind (memory leaks)
```bash
valgrind --leak-check=full ./build/bin/test_basic_connection
```

### Con Thread Sanitizer (race conditions)
```bash
# Ricompila con sanitizer
CFLAGS="-fsanitize=thread" make clean test
```

## Scenari di Test Pianificati

### 1. Connection Management
- [x] Station/Peer creation
- [ ] SNRM handshake (Primary → Secondary)
- [ ] UA response timing
- [ ] Connection timeout
- [ ] DISC disconnect sequence
- [ ] Collision handling (both send SNRM)

### 2. Data Transfer
- [ ] Small frames (<64 bytes)
- [ ] Large frames (max size)
- [ ] Window full (sender blocked)
- [ ] Multiple concurrent writes
- [ ] Flow control with backpressure

### 3. Error Recovery
- [ ] Single frame loss → REJ
- [ ] Multiple frame loss
- [ ] Checkpointed retransmission
- [ ] N(R) acknowledgment
- [ ] Sequence number wraparound

### 4. Concurrency & Thread Safety
- [ ] Multiple writers blocked/unblocked
- [ ] Read while window updating
- [ ] Disconnect during I/O
- [ ] Mutex correctness verification

### 5. Edge Cases
- [ ] Zero-length payload
- [ ] Rapid connect/disconnect cycles
- [ ] Malformed frames (CRC, length)
- [ ] Out-of-order N(S)

## Aggiungere Nuovi Test

1. Creare file in `scenarios/`:
```c
// test_my_scenario.c
#include "../../common/test_helpers.h"
#include "ioHdlc.h"

static bool test_my_feature(void) {
  TEST_ASSERT(condition, "description");
  return true;
}

int main(void) {
  RUN_TEST(test_my_feature);
  return (test_failures == 0) ? 0 : 1;
}
```

2. Aggiungere al `Makefile`:
```makefile
TEST_MY_SCENARIO = test_my_scenario
TEST_BINS += $(BIN_DIR)/$(TEST_MY_SCENARIO)

$(BIN_DIR)/$(TEST_MY_SCENARIO): $(OBJ_DIR)/test_my_scenario.o $(ALL_OBJS)
	$(CC) $^ -o $@ $(LDFLAGS)

$(OBJ_DIR)/test_my_scenario.o: $(SCENARIOS_DIR)/test_my_scenario.c
	$(CC) $(CFLAGS) -c $< -o $@
```

3. Build e test:
```bash
make test
```

## Prossimi Passi

1. **Completare adapter mock_stream → ioHdlcStream**
   - Implementare interfaccia port_ops
   - Thread RX per simulare interrupts
   - Callback on_rx, on_tx_done

2. **Implementare test end-to-end**
   - Due station connesse via mock streams
   - SNRM/UA handshake completo
   - Trasferimento dati bidirezionale

3. **Frame builder utilities**
   - Helper per costruire frame validi/invalidi
   - Injection di frame malformati
   - Scenari di errore controllati

4. **Port su ChibiOS**
   - Adattare OSAL per ChibiOS mock
   - Test su simulator o QEMU

## Note di Sviluppo

- **Mock Stream**: Usa buffer circolari in memoria con pthread per sincronizzazione
- **Peer Connection**: `mock_stream_connect()` permette comunicazione bidirezionale
- **Loopback**: Opzionale, TX automaticamente in RX per test singola station
- **Error Injection**: Supporto per simulare corruzione dati (TODO)
- **Timing**: Configurabile delay_us per simulare latenza reale (TODO)
