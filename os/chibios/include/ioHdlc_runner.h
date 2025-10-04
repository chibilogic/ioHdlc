/*
 * ChibiOS runner for HDLC station core (Option B).
 * Creates TX/RX threads and maps OS events/timers to core entry points.
 */
#ifndef IOHDLC_RUNNER_H_
#define IOHDLC_RUNNER_H_

#include "ch.h"
#include "ioHdlctypes.h"

#ifdef __cplusplus
extern "C" {
#endif

void ioHdlcRunnerStart(iohdlc_station_t *station);
void ioHdlcRunnerStop(iohdlc_station_t *station);

/* Reply timer control (runner side, maps to OS timers). */
void ioHdlcRunnerStartReplyTimer(iohdlc_station_peer_t *peer, uint32_t timeout_ms);
void ioHdlcRunnerRestartReplyTimer(iohdlc_station_peer_t *peer, uint32_t timeout_ms);
void ioHdlcRunnerStopReplyTimer(iohdlc_station_peer_t *peer);

#ifdef __cplusplus
}
#endif

#endif /* IOHDLC_RUNNER_H_ */
