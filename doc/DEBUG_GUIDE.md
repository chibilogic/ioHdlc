# Debug Session Guide for test_snrm_handshake

## Setup Complete ✓
- Debug configuration: `.vscode/launch.json`
- Build task: `.vscode/tasks.json`
- Binaries compiled with `-g3 -O0`

## How to Start Debugging

### Option 1: Debug from VS Code UI
1. Open [test_basic_connection.c](tests/common/scenarios/test_basic_connection.c)
2. Press `F5` or go to Run → Start Debugging
3. Select "Debug test_basic_connection" from the dropdown

### Option 2: Debug from Command Palette
1. Press `Ctrl+Shift+P`
2. Type "Debug: Select and Start Debugging"
3. Choose "Debug test_basic_connection"

### Option 3: No auto-build (faster iteration)
- Use "Debug test_basic_connection (no build)" to skip compilation

## Suggested Breakpoints

### In test_basic_connection.c (test_snrm_handshake):
- Line ~244: `ioHdlcRunnerStart(&station_primary)` - Before starting runners
- Line ~248: `ioHdlcStationLinkUp(...)` - Connection initiation
- Line ~259: After sleep, before checking connection state

### In ioHdlc.c (ioHdlcStationLinkUpEx):
- Line ~332: Function entry
- Line ~370: Loop iteration (retry logic)
- Line ~378: `s_runner_ops->broadcast_flags` - Signal TX thread
- Line ~380: `iohdlc_evt_wait_any_timeout` - Wait for response
- Line ~403: Timeout/retry path

### In ioHdlc_core.c (TX/RX entry points):
- Line ~1198: `ioHdlcTxEntry` - TX thread entry
- Line ~1250: `ioHdlcRxEntry` - RX thread entry (if exists)

### In mock_stream_adapter.c:
- Line ~28: `hdlc_tx_thread` - TX thread in runner
- Line ~33: `hdlc_rx_thread` - RX thread in runner
- Line ~123: `port_tx_submit` - Frame transmission
- Line ~149: `port_rx_submit` - RX buffer submission
- Line ~170: `adapter_rx_thread` - Background RX processing

### In ioHdlcosal.c (event system):
- Line ~352: `iohdlc_evt_broadcast_flags` - Event signaling
- Line ~304: `iohdlc_evt_wait_any_timeout` - Event waiting

## Key Variables to Watch

### In test:
- `station_primary.errorno` - Error code from primary
- `station_secondary.errorno` - Error code from secondary
- `peer_at_primary.ss_state` - Primary peer connection state
- `peer_at_secondary.ss_state` - Secondary peer connection state
- `ret` - Return value from LinkUp

### In LinkUp function:
- `retry_count` - Current retry attempt
- `evt` - Event mask returned from wait
- `flags` - Event flags received
- `p->um_cmd` - U-frame command being sent
- `p->um_state` - U-frame state
- `s->mode` - Station mode

### In adapter:
- `adapter->rx_buf` - Current RX buffer pointer
- `adapter->rx_len` - Buffer length
- `adapter->rx_pos` - Current position in buffer
- `adapter->running` - Thread running flag

## Debug Commands in GDB

```gdb
# Thread info
info threads

# Switch to specific thread
thread <n>

# Backtrace for all threads
thread apply all bt

# Watch variable changes
watch station_primary.errorno

# Conditional breakpoint
break ioHdlcStationLinkUpEx if retry_count > 0

# Print structure
p station_primary
p *peer_at_primary

# Follow fork/threads
set follow-fork-mode child
set detach-on-fork off
```

## Common Issues to Check

1. **Runner threads not starting**: Check pthread_create return in ioHdlcRunnerStart
2. **Events not delivered**: Watch iohdlc_evt_broadcast_flags and iohdlc_evt_wait_any_timeout
3. **RX buffer not filled**: Check adapter_rx_thread and mock_stream_read
4. **TX not sending**: Check port_tx_submit and mock_stream_write
5. **Timeout calculation**: Check `s->reply_timeout_ms * p->poll_retry_max`

## Next Steps After Debug

Once you identify the root cause:
- Fix the specific issue (runner ops, adapter RX, event signaling, etc.)
- Re-test with `make test` in tests/linux/
- Consider adding debug logging if issue is timing-related
