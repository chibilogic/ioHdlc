/*
 * HDLC command/event flag definitions shared across modules.
 */
#ifndef IOHDLC_EVENTS_H_
#define IOHDLC_EVENTS_H_

/* Event masks used for command management (EVT_CM_*). */
#define EVT_CM_C_RPLYTMO  0x0001  /* Command reply timer timeout. */
#define EVT_CM_I_RPLYTMO  0x0002  /* I-frame reply timer timeout. */
#define EVT_CM_UMRECVD    0x0004  /* Unnumbered command received. */
#define EVT_CM_CONNCHG    0x0008  /* Connection state change attempted. */
#define EVT_CM_CONNSTR    0x0010  /* Connection start/stop requested. */
#define EVT_CM_LINIDLE    0x0020  /* Line idle detected. */
#define EVT_CM_SSNDREQ    0x0040  /* Supervision frame transmit requested. */

#endif /* IOHDLC_EVENTS_H_ */
