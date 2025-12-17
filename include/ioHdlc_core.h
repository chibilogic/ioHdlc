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

/* Initialize core with runner-provided context (timers/events abstractions live in runner).
 * @param station    Station descriptor to initialize
 * @param fff_type   FFF type: 0 = no FFF (basic mode), 1 = TYPE 0 (1 byte), 2 = TYPE 1 (2 byte)
 */
bool ioHdlcCoreInit(iohdlc_station_t *station, uint8_t fff_type);

/* RX path: deliver a received frame to the core (from lower layers). */
void ioHdlcOnRxFrame(iohdlc_station_t *station, iohdlc_frame_t *fp);

/* Line idle notification (runner signals inter-frame idle). */
/* Returns station event flags the runner should signal (e.g., EVT_CM_LINIDLE). */
uint32_t ioHdlcOnLineIdle(iohdlc_station_t *station);

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
