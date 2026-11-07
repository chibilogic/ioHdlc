# ioHdlc Link Manager Utility

This optional utility monitors one ioHdlc peer and can reconnect it when the
application's physical-readiness policy permits a new attempt. It is built on
the application listener, peer-state, and link-management APIs and does not
alter the protocol core. Peer-state monitoring supports multipoint stations;
optional link-event monitoring requires a station with exactly one peer.

The application owns the manager object and the thread that calls
`ioHdlcLinkManagerRun()`. All configured callbacks run in that thread. The
manager does not start or stop the ioHdlc protocol runner.

## Callback Contracts

Zero-initialize `iohdlc_link_manager_config_t` and select at most one callback.
Both may be null if only active reconnection is needed. They are alternatives,
not two handlers invoked for the same notification.

| Callback | Scope | Notifications |
|---|---|---|
| `on_state_change(manager, state, arg)` | One peer, including on a multipoint station | Initial observation, then observed changes in that peer's state |
| `on_link_event(manager, state, events, arg)` | A station with exactly one peer | Initial observation, observed state changes, and link events even when state is unchanged |

The selected callback receives an initial observation after listener
registration, unless stopping has already been requested.

`on_state_change` retains the original contract. Station events wake the manager
to inspect its peer; an event for another peer does not invoke the callback if
the managed peer's state is unchanged. A disconnect/reconnect cycle completed
between observations may therefore be invisible. This is state observation,
not a record of every connection reset.

`on_link_event` additionally receives accumulated `IOHDLC_APP_LINK_UP`,
`IOHDLC_APP_LINK_DOWN`, and `IOHDLC_APP_LINK_LOST` flags. Flags are coalesced,
not ordered or counted, and the state is a separate current snapshot. A new UP
or a coalesced DOWN/UP is delivered even if the state is still CONNECTED:
an existing upper-layer stream must be retired before preparing another.
`events` can be zero at startup or when a state change is observed without
collected flags. Events carry no peer identifier, so this callback requires
exactly one peer in the entire station, not one manager per peer on a multipoint
station. That topology is an application precondition.

The original callback type, field, and signature remain available. Existing
zero-initialized or aggregate-initialized configurations keep the original
behavior with `on_link_event == NULL`; the new field is appended after
`callback_arg`. Code that assigns fields individually to an uninitialized
configuration must initialize the new field too. This is source compatibility,
not binary-layout compatibility: rebuild applications with the updated header.

## Preparing Upper-Layer Streams

Only `on_link_event`, on the manager's execution thread, may call
`ioHdlcLinkManagerCheck()`. It is not available as a per-peer continuity check
for `on_state_change` or multipoint integrations.

Before preparing a stream, require the callback's state to be
`IOHDLC_PEER_STATE_CONNECTED`. `Check()` does not test for CONNECTED: it can
return true for an unchanged disconnected state. It checks pending link events,
whether the current state still matches the notification, and stop requests,
without waiting for an event or consuming payload.

Call `Check()` before and after blocking preparation and before making the
stream available to other application threads. On false, release any partially
prepared stream and return without publishing it. Events collected by `Check()`
remain pending inside the manager for the next notification while it is running;
the callback must not acknowledge them again. A true result is a checkpoint,
not a lock or a guarantee against a subsequent disconnect or connection reset.

The integration owns protocol I/O retirement, native stream preparation and
optional authentication. Retire old I/O before preparing a replacement stream.
Clearing a retained EOF/reset belongs before a fresh upper-layer handshake,
never inside an active session. If that handshake fails while the HDLC peer
remains CONNECTED, the manager does not automatically retry stream setup in the
absence of a new event or observed state change. This is distinct from retrying
HDLC link-up while the peer is disconnected.

## Reconnection and Lifecycle

With `active == false`, the manager only monitors the peer. Active mode checks
`is_ready` before connection attempts (`NULL` means ready) and uses
`ioHdlcStationLinkUp()` unless a `connect` hook is supplied. Configure a valid
HDLC mode and a nonzero `retry_interval_ms`. Readiness checks and unsuccessful
attempts are repeated at that interval while the peer remains disconnected.
Readiness is not monitored while connected. An NRM secondary normally uses
passive mode and waits for its primary to establish the link.

Connection hooks follow the application API convention: zero on success and a
negative errno-style error on failure. Their return values are not propagated
by the manager; subsequent peer state determines whether reconnection is still
needed. No `iohdlc_errno` side channel is used.

`ioHdlcLinkManagerRun()` returns zero after stopping, `-EBUSY` if already
running, or a negative error from listener registration.

The configured event mask must be a dedicated bit other than
`IOHDLC_APP_EVT_MASK_DEFAULT`, which remains reserved for synchronous ioHdlc
operations. `ioHdlcLinkManagerStop()` requests termination and wakes event and
retry waits. It does not interrupt a callback or a synchronous connection call;
applications must bound their own blocking operations with suitable timeouts.
Stop neither disconnects the peer nor generates a final cleanup callback.

Wait for `Run()` to return, then complete application-owned stream cleanup
before destroying its resources. A stopped manager must be initialized again
before another run, never while the previous run or a callback is still active.

## Build Integration

Include `ioHdlcLinkManager.mk`, add `IOHDLC_LINK_MANAGER_SRCS` to the project
sources and `IOHDLC_LINK_MANAGER_INC` to its include paths.
