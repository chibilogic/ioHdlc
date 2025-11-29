/*
 * HDLC command/event flag definitions shared across modules.
 */
#ifndef IOHDLC_EVENTS_H_
#define IOHDLC_EVENTS_H_

/* Event masks used for command management. */
#define IOHDLC_EVT_C_RPLYTMO  0x0001  /* Command reply timer timeout. */
#define IOHDLC_EVT_I_RPLYTMO  0x0002  /* I-frame reply timer timeout. */
#define IOHDLC_EVT_UMRECVD    0x0004  /* Unnumbered command received. */
#define IOHDLC_EVT_CONNCHG    0x0008  /* Connection state change attempted. */
#define IOHDLC_EVT_CONNSTR    0x0010  /* Connection start/stop requested. */
#define IOHDLC_EVT_LINIDLE    0x0020  /* Line idle detected. */
#define IOHDLC_EVT_SSNDREQ    0x0040  /* Supervision frame transmit requested. */
#define IOHDLC_EVT_LINKDOWN   0x0080  /* Link down (retry count exhausted). */

#endif /* IOHDLC_EVENTS_H_ */
