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

/* Runner -> Core: entry points */

/* Initialize core with runner-provided context (timers/events abstractions live in runner). */
bool ioHdlcCoreInit(iohdlc_station_t *station);

/* Mode-specific TX/RX handlers (assigned to station->tx_fn and station->rx_fn). */
uint32_t nrmTx(iohdlc_station_t *s, iohdlc_station_peer_t *p, uint32_t cm_flags);
uint32_t abmTx(iohdlc_station_t *s, iohdlc_station_peer_t *p, uint32_t cm_flags);
void nrmRx(iohdlc_station_t *s, iohdlc_frame_t *fp);
void abmRx(iohdlc_station_t *s, iohdlc_frame_t *fp);

/* Runner ops registration (timer controls etc.). */
typedef struct ioHdlcRunnerOps {
  void (*start_reply_timer)(iohdlc_station_peer_t *peer,
                            iohdlc_timer_kind_t timer_kind,
                            uint32_t timeout_ms);
  void (*restart_reply_timer)(iohdlc_station_peer_t *peer,
                              iohdlc_timer_kind_t timer_kind,
                              uint32_t timeout_ms);
  void (*stop_reply_timer)(iohdlc_station_peer_t *peer,
                           iohdlc_timer_kind_t timer_kind);
  bool (*is_reply_timer_expired)(iohdlc_station_peer_t *peer,
                                 iohdlc_timer_kind_t timer_kind);
  /* Event helpers */
  uint32_t (*wait_events)(iohdlc_station_t *station, uint32_t mask);
  void (*broadcast_flags)(iohdlc_station_t *station, uint32_t flags);
  uint32_t (*get_events_flags)(iohdlc_station_t *station);
} ioHdlcRunnerOps;

void ioHdlcRegisterRunnerOps(const ioHdlcRunnerOps *ops);

/* Core → Runner: wrappers to control reply timer via registered ops. */
void ioHdlcStartReplyTimer(iohdlc_station_peer_t *peer,
                           iohdlc_timer_kind_t timer_kind,
                           uint32_t timeout_ms);
void ioHdlcRestartReplyTimer(iohdlc_station_peer_t *peer,
                             iohdlc_timer_kind_t timer_kind,
                             uint32_t timeout_ms);
void ioHdlcStopReplyTimer(iohdlc_station_peer_t *peer,
                          iohdlc_timer_kind_t timer_kind);
bool ioHdlcIsReplyTimerExpired(iohdlc_station_peer_t *peer,
                               iohdlc_timer_kind_t timer_kind);

/* Thread/task entry points (runner creates threads and calls these). */
void ioHdlcTxEntry(void *stationp);
void ioHdlcRxEntry(void *stationp);

/* helpers */
iohdlc_station_peer_t *ioHdlcNextPeer(iohdlc_station_t *station);

#ifdef __cplusplus
}
#endif

#endif /* IOHDLC_CORE_H_ */
