# ioHdlc Link Manager Utility

This optional utility monitors one ioHdlc peer and can reconnect it when the
application's physical-readiness policy permits a new attempt. It is built on
the existing application listener, peer-state, and link-management APIs and
does not alter the protocol core.

The application owns the manager object and the thread that calls
`ioHdlcLinkManagerRun()`. State callbacks run in that thread. Active mode uses
`ioHdlcStationLinkUp()` by default; readiness and connection hooks keep
platform-specific policy outside the utility.

`ioHdlcLinkManagerRun()` returns zero after stopping, `-EBUSY` if already
running, or a negative error from listener registration. Connection hooks
follow the application API convention: zero on success, negative errno-style
error on failure. No `iohdlc_errno` side channel is used.

The configured event mask must be a dedicated bit other than
`IOHDLC_APP_EVT_MASK_DEFAULT`, which remains reserved for synchronous ioHdlc
operations. `ioHdlcLinkManagerStop()` wakes event and retry waits; a stopped
manager must be initialized again before another run.

Include `ioHdlcLinkManager.mk`, add `IOHDLC_LINK_MANAGER_SRCS` to the project
sources and `IOHDLC_LINK_MANAGER_INC` to its include paths.
