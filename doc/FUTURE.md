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
