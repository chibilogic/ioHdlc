/*
 * OS-agnostic HDLC station core interface (Option B scaffolding).
 * Preserves original src/ioHdlc.c for reference.
 */
#ifndef IOHDLC_CORE_H_
#define IOHDLC_CORE_H_

#include "ioHdlctypes.h"
#include "ioHdlcframe.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Runner -> Core: entry points (use prefix ioHdlc* as requested) */

/* Initialize core with runner-provided context (timers/events abstractions live in runner). */
bool ioHdlcCoreInit(iohdlc_station_t *station);

/* RX path: deliver a received frame to the core (from lower layers). */
void ioHdlcOnRxFrame(iohdlc_station_t *station, iohdlc_frame_t *fp);

/* Timer event: reply timer elapsed for a given peer (runner calls this). */
/* Returns station event flags the runner should signal (e.g., EVT_CM_RPLYTMO). */
uint32_t ioHdlcOnReplyTimer(iohdlc_station_peer_t *peer);

/* Line idle notification (runner signals inter-frame idle). */
/* Returns station event flags the runner should signal (e.g., EVT_CM_LINIDLE). */
uint32_t ioHdlcOnLineIdle(iohdlc_station_t *station);

/* Generic station event injection (runner calls when it aggregates flags). */
void ioHdlcOnEvent(iohdlc_station_t *station, uint32_t event_flags);

/* Runner ops registration (timer controls etc.). */
typedef struct ioHdlcRunnerOps {
  void (*start_reply_timer)(iohdlc_station_peer_t *peer, uint32_t timeout_ms);
  void (*restart_reply_timer)(iohdlc_station_peer_t *peer, uint32_t timeout_ms);
  void (*stop_reply_timer)(iohdlc_station_peer_t *peer);
  /* Event helpers */
  uint32_t (*wait_events)(iohdlc_station_t *station, uint32_t mask);
  void (*broadcast_flags)(iohdlc_station_t *station, uint32_t flags);
} ioHdlcRunnerOps;

void ioHdlcRegisterRunnerOps(const ioHdlcRunnerOps *ops);

/* Core → Runner: wrappers to control reply timer via registered ops. */
void ioHdlcStartReplyTimer(iohdlc_station_peer_t *peer, uint32_t timeout_ms);
void ioHdlcRestartReplyTimer(iohdlc_station_peer_t *peer, uint32_t timeout_ms);
void ioHdlcStopReplyTimer(iohdlc_station_peer_t *peer);

/* Thread/task entry points (runner creates threads and calls these). */
void ioHdlcTxEntry(void *stationp);
void ioHdlcRxEntry(void *stationp);

#ifdef __cplusplus
}
#endif

#endif /* IOHDLC_CORE_H_ */
