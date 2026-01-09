/*
 * HDLC application event flag definitions.
 * These events are broadcasted on station->app_es for application consumption.
 */
#ifndef IOHDLC_APP_EVENTS_H_
#define IOHDLC_APP_EVENTS_H_

/* Application event masks for link management. */
#define IOHDLC_APP_LINK_UP       0x0001  /* Link established successfully (UA received). */
#define IOHDLC_APP_LINK_REFUSED  0x0002  /* Link connection refused (DM received). */
#define IOHDLC_APP_LINK_DOWN     0x0004  /* Link disconnected (UA/DM after DISC). */
#define IOHDLC_APP_LINK_TIMEOUT  0x0008  /* Link operation timeout (no response). */
#define IOHDLC_APP_LINK_LOST     0x0010  /* Link lost spontaneously (unexpected DM/timeout). */
#define IOHDLC_APP_DATA_READY    0x0020  /* Data available for reading (future use). */

#endif /* IOHDLC_APP_EVENTS_H_ */
