/*
    ioHdlc - Copyright (C) 2024 Isidoro Orabona

    GNU General Public License Usage

    ioHdlc software is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    ioHdlc software is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with ioHdlc software.  If not, see <http://www.gnu.org/licenses/>.

    Commercial License Usage

    Licensees holding valid commercial ioHdlc licenses may use this file in
    accordance with the commercial license agreement provided in accordance with
    the terms contained in a written agreement between you and Isidoro Orabona.
    For further information contact via email on github account.
 */

/**
 * @file    src/ioHdlc.c
 * @brief   HDLC Application Interface Implementation.
 * @details Implements public API functions for station link management
 *          and data transfer (LinkUp, LinkDown, Write, Read).
 *          OS-agnostic implementation using OSAL wrappers.
 *
 * @addtogroup hdlc
 * @{
 */

#include "ioHdlc.h"
#include "ioHdlc_core.h"
#include "ioHdlc_app_events.h"
#include "ioHdlcosal.h"
#include "ioHdlcqueue.h"
#include <string.h>

/*===========================================================================*/
/* Module local definitions.                                                 */
/*===========================================================================*/

/* Connection error codes (OS-agnostic, follow POSIX errno semantics) */
#ifndef EISCONN
#define EISCONN         106  /* Already connected */
#endif
#ifndef ENOTCONN
#define ENOTCONN        107  /* Not connected */
#endif
#ifndef ECONNREFUSED
#define ECONNREFUSED    111  /* Connection refused (DM received) */
#endif
#ifndef ETIMEDOUT
#define ETIMEDOUT       110  /* Timeout */
#endif
#ifndef EINVAL
#define EINVAL          22   /* Invalid argument */
#endif
#ifndef EEXIST
#define EEXIST          17   /* File exists */
#endif

/* Maximum connection retry attempts */
#define LINKUP_MAX_RETRIES   3
#define LINKDOWN_MAX_RETRIES 3

/*===========================================================================*/
/* Module exported variables.                                                */
/*===========================================================================*/

/*===========================================================================*/
/* Module local variables and types.                                         */
/*===========================================================================*/

/*===========================================================================*/
/* Module local functions.                                                   */
/*===========================================================================*/

/**
 * @brief   Convert operational mode to corresponding U-frame command.
 * @details Maps IOHDLC_OM_xxx mode constants to IOHDLC_U_xxx commands.
 *
 * @param[in] mode  Operational mode (IOHDLC_OM_NRM/ARM/ABM)
 * @return          U-frame command code, or 0 if mode not supported
 */
static uint8_t mode_to_u_cmd(uint8_t mode) {
  switch (mode) {
    case IOHDLC_OM_NRM:
      return IOHDLC_U_SNRM;
    case IOHDLC_OM_ARM:
      return IOHDLC_U_SARM;
    case IOHDLC_OM_ABM:
      return IOHDLC_U_SABM;
    default:
      return 0;  /* Invalid mode */
  }
}

/**
 * @brief   Convert U-frame command to operational mode.
 * @details Maps IOHDLC_U_xxx commands to IOHDLC_OM_xxx mode constants.
 *
 * @param[in] u_cmd  U-frame command code
 * @return           Operational mode, or 0 if command not a set-mode
 */
static uint8_t u_cmd_to_mode(uint8_t u_cmd) {
  switch (u_cmd) {
    case IOHDLC_U_SNRM:
      return IOHDLC_OM_NRM;
    case IOHDLC_U_SARM:
      return IOHDLC_OM_ARM;
    case IOHDLC_U_SABM:
      return IOHDLC_OM_ABM;
    default:
      return 0;  /* Not a set-mode command */
  }
}

/**
 * @brief   Find peer by address in station's peer list.
 * @details Iterates through the circular linked list of peers.
 *
 * @param[in] s         Station descriptor
 * @param[in] peer_addr Peer address to search for
 * @return              Pointer to peer descriptor, or NULL if not found
 */
iohdlc_station_peer_t *addr2peer(iohdlc_station_t *s, uint32_t peer_addr) {
  iohdlc_station_peer_t *p;

  /* Traverse circular peer list */
  for (p = s->peers.next; p != (iohdlc_station_peer_t *)&s->peers; p = p->next) {
    if (p->addr == peer_addr)
      return p;
  }
  
  return NULL;
}

/**
 * @brief   Initialize and add a peer to an HDLC station.
 * @details Initializes peer structure, queues, semaphores and adds to station's peer list.
 *          
 * @param[in] s     Station descriptor
 * @param[in] peer  Peer structure to initialize
 * @param[in] addr  Peer address on the data link
 * @param[in] mifl  Maximum information field length
 * 
 * @return          0 on success, -1 on error
 * @retval 0        Peer successfully added to station
 * @retval -1       Error occurred:
 *                  - EINVAL: Secondary station not in NDM/ADM mode
 *                  - EEXIST: Peer with same address already exists
 * 
 * @note Station errorno field contains detailed error code on failure.
 */
int32_t ioHdlcAddPeer(iohdlc_station_t *s, iohdlc_station_peer_t *peer,
                      uint32_t addr, uint32_t mifl) {
  /* Secondary stations can only add peer when in disconnected mode */
  if (!(s->flags & IOHDLC_FLG_PRI) &&
      (s->mode != IOHDLC_OM_NDM) && (s->mode != IOHDLC_OM_ADM)) {
    s->errorno = EINVAL;
    return -1;
  }

  /* Check that addr does not already exist */
  if (addr2peer(s, addr) != NULL) {
    s->errorno = EEXIST;
    return -1;
  }

  /* Initialize the peer structure */
  memset(peer, 0, sizeof(*peer));
  peer->addr = addr;
  peer->stationp = s;
  peer->kr = peer->ks = s->modmask;
  peer->miflr = peer->mifls = mifl;
  peer->poll_retry_max = 3;  /* Default: max 3 retries before link down */
  
  /* Initialize queues */
  ioHdlc_frameq_init(&peer->i_recept_q);
  ioHdlc_frameq_init(&peer->i_retrans_q);
  ioHdlc_frameq_init(&peer->i_trans_q);
  
  /* Initialize TX flow control semaphore */
  iohdlc_bsem_init(&peer->tx_sem, false);  /* Initially taken (no flow) */
  
  /* Add peer to station's peer list */
  peer->next = (iohdlc_station_peer_t *)&s->peers;
  peer->prev = s->peers.prev;
  s->peers.prev->next = peer;
  s->peers.prev = peer;
  
  return 0;
}

/*===========================================================================*/
/* Module exported functions.                                                */
/*===========================================================================*/

/**
 * @brief   Establish data link connection with a peer (extended version).
 * @details Initiates connection by sending appropriate U-frame command
 *          (SNRM/SARM/SABM) and waiting for UA response. Implements
 *          retry logic with configurable timeout.
 *          
 *          Primary station behavior:
 *          - Sends set-mode command with P=1
 *          - Waits for UA response with F=1
 *          - Retries up to LINKUP_MAX_RETRIES on timeout
 *          - Returns error on DM (connection refused)
 *          
 *          Secondary station behavior:
 *          - Returns immediately (waits for primary to initiate)
 *          
 * @param[in] s         Station descriptor
 * @param[in] peer_addr Peer address to connect to
 * @param[in] mode      Desired operational mode (IOHDLC_OM_NRM/ARM/ABM)
 * @param[in] evt_mask  Event mask for listener registration (e.g., EVENT_MASK(31))
 * 
 * @return              0 on success, -1 on error
 * @retval 0            Link established successfully
 * @retval -1           Error occurred:
 *                      - EISCONN: Already connected
 *                      - EINVAL: Invalid mode or peer not found
 *                      - ETIMEDOUT: No response after max retries
 *                      - ECONNREFUSED: Peer sent DM (refused connection)
 * 
 * @note Station errorno field contains detailed error code on failure.
 * @note This function blocks until connection completes or fails.
 * @note Uses protocol-level retry (no application timeout parameter).
 * @note Uses app_es event source to avoid conflicts with core events.
 */
int32_t ioHdlcStationLinkUpEx(iohdlc_station_t *s, uint32_t peer_addr, 
                              uint8_t mode, eventmask_t evt_mask) {
  iohdlc_station_peer_t *p;
  iohdlc_event_listener_t listener;
  uint8_t u_cmd;
  int retry_count;

  /* Find peer by address */
  p = addr2peer(s, peer_addr);
  if (p == NULL) {
    s->errorno = EINVAL;
    return -1;
  }

  /* Check if already connected */
  if (p->ss_state & IOHDLC_SS_ST_CONN) {
    s->errorno = EISCONN;
    return -1;
  }

  /* Secondary stations wait for primary to initiate connection */
  if (IOHDLC_IS_SEC(s))
    return 0;

  /* Validate mode and get corresponding U-frame command */
  u_cmd = mode_to_u_cmd(mode);
  if (u_cmd == 0) {
    s->errorno = EINVAL;
    return -1;
  }

  /* Register listener on app_es for link events */
  iohdlc_evt_register(&s->app_es, &listener, evt_mask,
                      IOHDLC_APP_LINK_UP | IOHDLC_APP_LINK_REFUSED);

  /* Connection retry loop */
  for (retry_count = 0; retry_count < LINKUP_MAX_RETRIES; retry_count++) {
    /* Set unnumbered command in peer descriptor */
    p->um_cmd = u_cmd;
    p->um_state |= IOHDLC_UM_SENDING;
    
    /* Clear disconnected-mode flag (we're attempting connection) */
    p->ss_state &= ~IOHDLC_SS_ST_DISM;

    /* Signal TX task to send the command */
    s_runner_ops->broadcast_flags(s, IOHDLC_EVT_CONNSTR);

    /* Wait for app event with timeout */
    uint32_t timeout_ms = s->reply_timeout_ms * p->poll_retry_max;
    eventmask_t evt = iohdlc_evt_wait_any_timeout(evt_mask, timeout_ms);

    if (evt != 0) {
      /* Event received: check which one */
      eventflags_t flags = iohdlc_evt_get_and_clear_flags(&listener);
      
      if (flags & IOHDLC_APP_LINK_UP) {
        /* Success: link established */
        iohdlc_evt_unregister(&s->app_es, &listener);
        return 0;
      } else if (flags & IOHDLC_APP_LINK_REFUSED) {
        /* DM received: peer refused connection */
        iohdlc_evt_unregister(&s->app_es, &listener);
        s->errorno = ECONNREFUSED;
        return -1;
      }
    }
    /* Timeout: retry */
  }

  /* All retries exhausted */
  iohdlc_evt_unregister(&s->app_es, &listener);
  s->errorno = ETIMEDOUT;
  return -1;
}

/**
 * @brief   Terminate data link connection with a peer (extended version).
 * @details Sends DISC command and waits for UA/DM response.
 *          Implements retry logic with configurable timeout.
 *          
 *          Primary station behavior:
 *          - Sends DISC with P=1
 *          - Waits for UA or DM response with F=1
 *          - Retries up to LINKDOWN_MAX_RETRIES on timeout
 *          - Clears peer state variables and queues on success
 *          
 *          Secondary station behavior:
 *          - Returns immediately (waits for primary to disconnect)
 *          
 * @param[in] s         Station descriptor
 * @param[in] peer_addr Peer address to disconnect from
 * @param[in] evt_mask  Event mask for listener registration (e.g., EVENT_MASK(31))
 * 
 * @return              0 on success, -1 on error
 * @retval 0            Link terminated successfully
 * @retval -1           Error occurred:
 *                      - ENOTCONN: Not connected or peer not found
 *                      - ETIMEDOUT: No response after max retries
 * 
 * @note Station errorno field contains detailed error code on failure.
 * @note This function blocks until disconnection completes or fails.
 * @note Peer state is reset (queues cleared, variables reset) on success.
 * @note Uses app_es event source to avoid conflicts with core events.
 */
int32_t ioHdlcStationLinkDownEx(iohdlc_station_t *s, uint32_t peer_addr,
                                eventmask_t evt_mask) {
  iohdlc_station_peer_t *p;
  iohdlc_event_listener_t listener;
  int retry_count;

  /* Find peer by address */
  p = addr2peer(s, peer_addr);
  if (p == NULL) {
    s->errorno = ENOTCONN;
    return -1;
  }

  /* Check if already disconnected */
  if (!(p->ss_state & IOHDLC_SS_ST_CONN)) {
    s->errorno = ENOTCONN;
    return -1;
  }

  /* Secondary stations wait for primary to disconnect */
  if (IOHDLC_IS_SEC(s))
    return 0;

  /* Register listener on app_es for link events */
  iohdlc_evt_register(&s->app_es, &listener, evt_mask,
                      IOHDLC_APP_LINK_DOWN);

  /* Disconnection retry loop */
  for (retry_count = 0; retry_count < LINKDOWN_MAX_RETRIES; retry_count++) {
    /* Set DISC command in peer descriptor */
    p->um_cmd = IOHDLC_U_DISC;
    p->um_state |= IOHDLC_UM_SENDING;
    
    /* Clear disconnected-mode flag (not yet confirmed) */
    p->ss_state &= ~IOHDLC_SS_ST_DISM;

    /* Signal TX task to send DISC */
    s_runner_ops->broadcast_flags(s, IOHDLC_EVT_CONNSTR);

    /* Wait for app event with timeout */
    uint32_t timeout_ms = s->reply_timeout_ms * p->poll_retry_max;
    eventmask_t evt = iohdlc_evt_wait_any_timeout(evt_mask, timeout_ms);

    if (evt != 0) {
      /* Event received: check which one */
      eventflags_t flags = iohdlc_evt_get_and_clear_flags(&listener);
      
      if (flags & IOHDLC_APP_LINK_DOWN) {
        /* Success: link terminated */
        iohdlc_evt_unregister(&s->app_es, &listener);
        return 0;
      }
    }
    /* Timeout: retry */
  }

  /* All retries exhausted */
  iohdlc_evt_unregister(&s->app_es, &listener);
  s->errorno = ETIMEDOUT;
  return -1;
}

/**
 * @brief   Write data to peer via HDLC I-frames.
 * @details Allocates frame, copies data to info field, queues for transmission.
 *          Blocks on flow control if window full or pool low watermark reached.
 *          
 * @param[in] peer      Peer descriptor
 * @param[in] buf       Data buffer to transmit
 * @param[in] count     Number of bytes to send
 * @param[in] timeout_ms Timeout in milliseconds (IOHDLC_WAIT_FOREVER for blocking)
 * 
 * @return              Bytes written on success, -1 on error
 * @retval count        All data successfully queued
 * @retval -1           Error occurred (check station->errorno)
 * 
 * @note Blocks if i_pending_count >= 2*ks OR pool is LOW_WATER.
 * @note Sets address, N(S), frame ID; N(R) and P/F set during TX.
 */
ssize_t ioHdlcWriteTmo(iohdlc_station_peer_t *peer, const void *buf, 
                        size_t count, uint32_t timeout_ms) {
  /* TODO: Implement data transmission logic
   * 1. Wait on tx_sem if flow control active
   * 2. Allocate frame from pool
   * 3. Copy buffer to frame info field
   * 4. Set address, N(S) = vs (pre-increment), I-frame ID
   * 5. Calculate elen, valorize FFF
   * 6. Queue to i_trans_q, increment i_pending_count
   * 7. Signal IOHDLC_EVT_ISNDREQ to TX task
   */
  (void)peer;
  (void)buf;
  (void)count;
  (void)timeout_ms;
  return -1;  /* Not yet implemented */
}

/**
 * @brief   Read data from peer via HDLC I-frames.
 * @details Blocks until I-frame available in reception queue, copies to buffer.
 *          Triggers RR when pool returns to normal watermark.
 *          
 * @param[in] peer      Peer descriptor
 * @param[out] buf      Buffer to receive data
 * @param[in] count     Maximum bytes to read
 * @param[in] timeout_ms Timeout in milliseconds (IOHDLC_WAIT_FOREVER for blocking)
 * 
 * @return              Bytes read on success, -1 on error
 * @retval >0           Number of bytes read
 * @retval -1           Error occurred (check station->errorno)
 * 
 * @note Blocks until frame available or timeout.
 * @note Releases frame back to pool (may trigger watermark transition).
 */
ssize_t ioHdlcReadTmo(iohdlc_station_peer_t *peer, void *buf, 
                      size_t count, uint32_t timeout_ms) {
  /* TODO: Implement data reception logic
   * 1. Wait on i_recept_q non-empty with timeout
   * 2. Remove frame from queue
   * 3. Copy info field to buffer (up to count bytes)
   * 4. Release frame (triggers watermark check → RR if NORMAL)
   * 5. Return bytes read
   */
  (void)peer;
  (void)buf;
  (void)count;
  (void)timeout_ms;
  return -1;  /* Not yet implemented */
}

/** @} */
