# Test Architecture - OS-Agnostic Design

## Filosofia

L'architettura dei test ioHdlc segue il principio di **massima portabilità**: i test che usano solo interfacce astratte sono condivisi tra tutte le piattaforme, mentre solo i test specifici dell'OS rimangono separati.

## Struttura dei Test

### Test OS-Agnostic (`tests/common/scenarios/`)

Questi test usano **solo interfacce astratte** definite nel core ioHdlc:

1. **test_frame_pool.c**
   - Interfaccia: `ioHdlcFramePool` (abstract base class)
   - Macro: `hdlcTakeFrame()`, `hdlcReleaseFrame()`, `hdlcAddRef()`
   - Testa: Allocazione, deallocazione, refcount, watermark
   - Portabilità: ✅ Funziona su Linux, ChibiOS, RTOS generico

2. **test_basic_connection.c**
   - Interfaccia: `ioHdlc` core (stazioni, peer, handshake)
   - Testa: Creazione station/peer, SNRM/UA, timeout
   - Portabilità: ✅ Funziona su qualsiasi piattaforma

**Caratteristiche:**
- Nessun `#include` di header OS-specific
- Nessuna chiamata diretta a pthread, malloc, etc.
- Solo interfacce VMT-based e macro astratti
- Stesso sorgente compilato per tutte le piattaforme

### Test Platform-Specific

#### Linux (`tests/linux/scenarios/`)

1. **test_mock_stream.c**
   - Testa implementazione mock stream con pthread
   - Usa `mock_stream.h` (POSIX-specific)
   - Valida buffer circolari, loopback, peer connection

#### ChibiOS (futuro `tests/chibios/scenarios/`)

- Test con stream UART reale
- Test di integrazione con DMA
- Performance benchmarking

## Build System

### Linux Makefile

```makefile
# Test comuni (OS-agnostic)
COMMON_TEST_SRCS = \
    $(COMMON_SCENARIOS_DIR)/test_frame_pool.c \
    $(COMMON_SCENARIOS_DIR)/test_basic_connection.c

# Test Linux-specific
LINUX_TEST_SRCS = \
    $(SCENARIOS_DIR)/test_mock_stream.c
```

### ChibiOS Makefile (futuro)

```makefile
# Stessi test comuni
COMMON_TEST_SRCS = \
    $(COMMON_SCENARIOS_DIR)/test_frame_pool.c \
    $(COMMON_SCENARIOS_DIR)/test_basic_connection.c

# Test ChibiOS-specific
CHIBIOS_TEST_SRCS = \
    $(SCENARIOS_DIR)/test_real_uart.c
```

## Vantaggi

### 1. Riuso Massimo
- Scrivi il test **una volta**
- Eseguilo su **tutte le piattaforme**
- Mantieni **una sola copia**

### 2. Garanzia di Portabilità
Se `test_frame_pool.c` compila e passa su Linux, sappiamo che:
- L'interfaccia `ioHdlcFramePool` è correttamente implementata
- I macro `hdlcTakeFrame` etc. funzionano
- Il comportamento è consistente cross-platform

### 3. Manutenibilità
- Bug fix: modifica in un solo posto
- Nuovi test: automaticamente disponibili per tutte le piattaforme
- Refactoring: impatto ridotto

### 4. Coverage Uniforme
Ogni piattaforma ottiene:
- ✅ Test frame pool completo (5 scenari)
- ✅ Test HDLC core (4+ scenari)
- ✅ Test stream driver (N scenari)
- ✅ Test specifici per quella piattaforma

## Linee Guida per Nuovi Test

### Test deve essere OS-agnostic se:
- ✅ Usa solo interfacce definite in `include/*.h`
- ✅ Usa solo macro pubblici (`hdlcXxx`, `iohdlc_xxx`)
- ✅ Non fa assunzioni su threading model
- ✅ Non usa tipi OS-specific (`pthread_t`, `Thread*`)

→ **Metti in `tests/common/scenarios/`**

### Test deve essere platform-specific se:
- ❌ Usa API OS-specific (pthread, chThdCreate)
- ❌ Testa implementazione OSAL
- ❌ Testa mock/driver specifici
- ❌ Richiede hardware specifico

→ **Metti in `tests/<platform>/scenarios/`**

## Esempi Futuri

### OS-Agnostic (common)
```c
// test_i_frame_transfer.c - Common
#include "ioHdlc.h"
#include "ioHdlcframe.h"

int test_i_frame_sequence(void) {
    iohdlc_frame_t *frame = hdlcTakeFrame(pool);
    // ... test I-frame sequencing ...
    hdlcReleaseFrame(pool, frame);
}
```

### Platform-Specific (linux)
```c
// test_pthread_safety.c - Linux
#include "ioHdlcosal.h"
#include <pthread.h>

int test_concurrent_allocation(void) {
    pthread_t threads[10];
    // ... test thread-safety ...
}
```

## Metriche Attuali

**OS-Agnostic Tests:**
- test_frame_pool.c: 29 assertions, 5 scenarios
- test_basic_connection.c: 4 assertions, 4 scenarios
- **Total: 33 assertions portabili**

**Linux-Specific Tests:**
- test_mock_stream.c: 21 assertions, 5 scenarios

**Coverage:**
- Frame Pool: 100% (init, alloc, refcount, watermark, exhaustion)
- HDLC Core: 10% (stubs - needs implementation)
- Mock Stream: 100%

## Next Steps

1. Implementare test_hdlc_handshake.c (OS-agnostic)
2. Implementare test_i_frame_windowing.c (OS-agnostic)
3. Creare tests/chibios/ per ARM target
4. Aggiungere CI per eseguire test su multiple piattaforme
