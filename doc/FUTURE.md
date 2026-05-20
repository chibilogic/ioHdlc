# Future Work

## Adaptive Reply Timeout

`T1` can be tuned from protocol-level round-trip samples instead of physical
transport timing. This is more useful because the sample includes the real
system behavior: transmission time, peer processing, backend rearm, scheduling,
and the return frame.

The natural sample depends on the operating mode:

- In `NRM/TWA`, measure from the primary transmission of a frame with `P=1` to
  the first valid response from the secondary.
- In `TWS`, measure from transmission of `P=1` to reception of the matching
  `F=1`.

Only clean cycles should update the estimate. Samples should be discarded when
there is a timeout, retransmission, RX error, `FRMR`, `DM`, `DISC`, link-state
change, or any condition that makes the received response not attributable to
the current poll cycle.

The estimated RTT should be filtered, for example with an EWMA, and then used to
derive `T1` with explicit lower and upper clamps plus a safety margin. All
updates must stay outside ISR paths; ISR code should remain limited to the
minimum timestamp/callback work required by the backend.

## Checkpoint and REJ Recovery Semantics

The interaction between P/F checkpoint recovery and REJ recovery should be made
more explicit and closer to ISO 13239 sections 5.6.2.1 and 5.6.2.2.

The current implementation intentionally behaves as Go-Back-N because SREJ is
not implemented and the effective receive window is one. Under this constraint,
after `N(R)` processing it is acceptable for checkpoint or REJ recovery to move
the whole retransmission queue back to the transmit queue.

The part to refine is the mutual inhibition logic:

- A REJ received after checkpoint retransmission should be inhibited only when
  it would start retransmission from the same particular I-frame already
  selected by checkpoint recovery, identified by the same `N(S)` in the same
  numbering cycle.
- Conversely, checkpoint recovery should be inhibited only when an already
  actioned received REJ covers the same particular I-frame.
- The local `rej_actioned` state currently describes a sent REJ exception on the
  receive side. It should not be confused with the state needed to remember a
  received REJ that has already been actioned on the transmit side.
- `chkpt_actioned` should remain precise enough to represent the first I-frame
  selected by checkpoint recovery; REJ inhibition should compare against that
  value instead of treating any non-zero checkpoint state as a global inhibitor.
- Comments around `checkpointRetransmit()` and the `IOHDLC_S_REJ` handling
  should be updated once the state model is clarified.

Validation should include traces with overlapping checkpoint and REJ recovery,
checking for each REJ the requested `N(S)`, the first matching retransmission,
and the frames emitted before that retransmission.
