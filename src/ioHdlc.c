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
#include "ioHdlclist.h"
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
#ifndef ENOMEM
#define ENOMEM          12   /* Out of memory */
#endif
#ifndef EAGAIN
#define EAGAIN          11   /* Try again */
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

/* Runner ops registration for core event broadcasting */
extern const ioHdlcRunnerOps *s_runner_ops;

/*===========================================================================*/
/* Module local functions.                                                   */
/*===========================================================================*/

/**
 * @brief   Initialize HDLC station with configuration.
 * @details Initializes a station descriptor with the provided configuration:
 *          - Calculates modulus parameters (modmask, pfoctet, ctrl_size)
 *          - Sets operational mode and flags
 *          - Initializes peer list and event sources
 *          - Configures optional functions (REJ, FFF, STB)
 *          - Sets TX/RX handlers based on mode
 *
 * @param[in] ioHdlcsp      Station descriptor to initialize
 * @param[in] ioHdlcsconfp  Configuration parameters
 * @return                  0 on success, -1 on error (check ioHdlcsp->errorno)
 * 
 * @note The caller must have prepared:
 *       - Frame pool (ioHdlcsconfp->fpp)
 *       - Driver implementation (ioHdlcsconfp->driver)
 *       - Optional: physical device and config (phydriver, phydriver_config)
 * 
 * @api
 */
int32_t ioHdlcStationInit(iohdlc_station_t *ioHdlcsp,
                          const iohdlc_station_config_t *ioHdlcsconfp) {
  uint32_t mod2 = 0;
  uint8_t mode = ioHdlcsconfp->mode;

  /* Validate mode */
  if ((mode != IOHDLC_OM_NDM) && (mode != IOHDLC_OM_ADM) &&
      (mode != IOHDLC_OM_NRM) && (mode != IOHDLC_OM_ARM) &&
      (mode != IOHDLC_OM_ABM)) {
    ioHdlcsp->errorno = EINVAL;
    return -1;
  }

  /* Basic station parameters */
  ioHdlcsp->addr = ioHdlcsconfp->addr;
  ioHdlcsp->flags = ioHdlcsconfp->flags;
  ioHdlcsp->mode = mode;

  /* Calculate modulus parameters */
  mod2 = ioHdlcsconfp->log2mod;
  ioHdlcsp->modmask = (1U << mod2) - 1;  /* 7, 127, 32767, 2147483647 */
  ioHdlcsp->pfoctet = (mod2 + 1) / 8;
  ioHdlcsp->ctrl_size = (mod2 == 3) ? 1 : (ioHdlcsp->pfoctet * 2);

  /* Default timeout */
  ioHdlcsp->reply_timeout_ms = 100;

  /* Frame pool and driver */
  ioHdlcsp->frame_pool = ioHdlcsconfp->fpp;
  ioHdlcsp->driver = ioHdlcsconfp->driver;

  /* Initialize peer list */
  ioHdlc_peerl_init(&ioHdlcsp->peers);
  ioHdlcsp->c_peer = NULL;

  /* Initialize event sources (OS-agnostic via osal) */
  iohdlc_evt_init(&ioHdlcsp->cm_es);
  iohdlc_evt_init(&ioHdlcsp->app_es);

  /* Initialize runner state */
  ioHdlcsp->stop_requested = false;
  ioHdlcsp->runner_context = NULL;

  /* Set TX/RX handlers based on mode */
  if (mode == IOHDLC_OM_NRM) {
    ioHdlcsp->tx_fn = nrmTx;
    ioHdlcsp->rx_fn = nrmRx;
  } else if (mode == IOHDLC_OM_ARM) {
    ioHdlcsp->tx_fn = armTx;
    ioHdlcsp->rx_fn = armRx;
  } else if (mode == IOHDLC_OM_ABM) {
    ioHdlcsp->tx_fn = abmTx;
    ioHdlcsp->rx_fn = abmRx;
  }

  /* Configure optional functions */
  if (ioHdlcsconfp->optfuncs != NULL) {
    /* User provided custom optional functions */
    memcpy(ioHdlcsp->optfuncs, ioHdlcsconfp->optfuncs, sizeof(ioHdlcsp->optfuncs));
  } else {
    /* Use default optional functions: REJ, SST, FFF enabled */
    memset(ioHdlcsp->optfuncs, 0, sizeof(ioHdlcsp->optfuncs));
    ioHdlcsp->optfuncs[IOHDLC_OPT_REJ_OCT] |= IOHDLC_OPT_REJ;
    ioHdlcsp->optfuncs[IOHDLC_OPT_SST_OCT] |= IOHDLC_OPT_SST;
    ioHdlcsp->optfuncs[IOHDLC_OPT_FFF_OCT] |= IOHDLC_OPT_FFF;
    ioHdlcsp->optfuncs[IOHDLC_OPT_INH_OCT] |= IOHDLC_OPT_INH;
    
  }

  /* Initialize fast-access critical flags from optfuncs */
  ioHdlcsp->flags_critical = 0;
  
  if (ioHdlcsp->optfuncs[IOHDLC_OPT_FFF_OCT] & IOHDLC_OPT_FFF) {
    ioHdlcsp->flags_critical |= IOHDLC_CFLG_FFF;
    ioHdlcsp->frame_offset = 1;  /* FFF present: addr starts at offset 1 */
  } else {
    ioHdlcsp->frame_offset = 0;  /* No FFF: addr starts at offset 0 */
  }
  
  if (ioHdlcsp->optfuncs[IOHDLC_OPT_REJ_OCT] & IOHDLC_OPT_REJ) {
    ioHdlcsp->flags_critical |= IOHDLC_CFLG_REJ;
  }
  
  if (ioHdlcsp->optfuncs[IOHDLC_OPT_STB_OCT] & IOHDLC_OPT_STB) {
    ioHdlcsp->flags_critical |= IOHDLC_CFLG_STB;
  }

  /* Configure driver settings before starting */
  if (ioHdlcsconfp->phydriver != NULL && ioHdlcsp->driver != NULL) {
    /* Set transparency option (STB - Start/Stop with Basic Transparency) */
    bool apply_transparency = (ioHdlcsp->optfuncs[IOHDLC_OPT_STB_OCT] & IOHDLC_OPT_STB) != 0;
    hdlcApplyTransparency(ioHdlcsp->driver, apply_transparency);
    
    /* Set frame format field option (FFF) */
    bool has_frame_format = (ioHdlcsp->optfuncs[IOHDLC_OPT_FFF_OCT] & IOHDLC_OPT_FFF) != 0;
    hdlcHasFrameFormat(ioHdlcsp->driver, has_frame_format);
    if (has_frame_format && (ioHdlcsp->optfuncs[IOHDLC_OPT_INH_OCT] & IOHDLC_OPT_INH)) {
      /* It takes precedence over STB. */
      hdlcApplyTransparency(ioHdlcsp->driver, false);
    }
  }

  /* Start driver if physical device provided */
  if (ioHdlcsconfp->phydriver != NULL && ioHdlcsp->driver != NULL) {
    ioHdlcsp->driver->vmt->start(ioHdlcsp->driver, 
                                  ioHdlcsconfp->phydriver,
                                  ioHdlcsconfp->phydriver_config,
                                  ioHdlcsp->frame_pool);
  }

  ioHdlcsp->errorno = 0;
  return 0;
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
 *          Maximum Information Field Length (mifl) is automatically calculated from
 *          station's frame pool size minus protocol overhead.
 *          
 * @param[in] s     Station descriptor
 * @param[in] peer  Peer structure to initialize (allocated by caller)
 * @param[in] addr  Peer address on the data link
 * 
 * @return          0 on success, -1 on error
 * @retval 0        Peer successfully added to station
 * @retval -1       Error occurred:
 *                  - EINVAL: Secondary station not in NDM/ADM mode
 *                  - EEXIST: Peer with same address already exists
 * 
 * @note Station errorno field contains detailed error code on failure.
 * @note mifl is calculated as: framesize - (FFF + ADDR + CTRL + FCS)
 *       For modulo 8: framesize - (1 + 1 + 1 + 2) = framesize - 5 (if FFF enabled)
 * 
 * @api
 */
int32_t ioHdlcAddPeer(iohdlc_station_t *s, iohdlc_station_peer_t *peer,
                      uint32_t addr) {
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

  /* Calculate mifl from frame pool size minus overhead.
     Overhead = FFF (frame_offset) + ADDR (1) + CTRL (ctrl_size) + FCS (2) */
  uint32_t overhead = s->frame_offset + 1 + s->ctrl_size + 2;
  uint32_t mifl = (s->frame_pool->framesize > overhead) ? 
                  (s->frame_pool->framesize - overhead) : 64;  /* Fallback to 64 */

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
  
  /* Initialize semaphores and mutex */
  iohdlc_bsem_init(&peer->tx_sem, false);       /* Initially not taken (no flow) */
  iohdlc_bsem_init(&peer->i_recept_sem, true);  /* Initially taken (no data) */
  iohdlc_mutex_init(&peer->state_mutex);        /* Priority-inheriting mutex for state */
  
  /* Initialize virtual timers (reply and I-frame reply) */
  iohdlc_vt_init(&peer->reply_tmr);
  iohdlc_vt_init(&peer->i_reply_tmr);
  peer->reply_tmr.peer = peer;
  peer->i_reply_tmr.peer = peer;
  
  /* Initialize partial read state */
  peer->partial_read_frame = NULL;
  peer->partial_read_offset = 0;
  
  /* Add peer to station's peer list */
  peer->next = (iohdlc_station_peer_t *)&s->peers;
  peer->prev = s->peers.prev;
  s->peers.prev->next = peer;
  s->peers.prev = peer;
  
  /* Initialize c_peer to first peer if not already set */
  if (s->c_peer == NULL) {
    s->c_peer = peer;
  }
  
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
  u_cmd = IOHDLC_MODE_TO_UCMD(mode);
  if (u_cmd == 0) {
    s->errorno = EINVAL;
    return -1;
  }

  /* Register listener on app_es for link events */
  iohdlc_evt_register(&s->app_es, &listener, evt_mask,
                      IOHDLC_APP_LINK_UP | IOHDLC_APP_LINK_REFUSED);

  /* Set mode for this connection attempt */
  s->mode = mode;
  
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
 * @details Fragments data into I-frames if necessary, queues for transmission.
 *          Blocks on flow control if window full or pool low watermark reached.
 *          Loops until all bytes are queued or error occurs.
 *          
 * @param[in] peer       Peer descriptor
 * @param[in] buf        Data buffer to transmit
 * @param[in] count      Number of bytes to send
 * @param[in] timeout_ms Timeout in milliseconds (IOHDLC_WAIT_FOREVER for blocking)
 * 
 * @return               Bytes written on success, -1 on error
 * @retval count         All data successfully queued
 * @retval -1            Error occurred (check station->errorno)
 * 
 * @note Blocks if i_pending_count >= 2*ks OR pool is LOW_WATER.
 * @note Sets address, N(S), frame ID; N(R) and P/F set during TX.
 * @note Automatically fragments data if count > mifls.
 */
ssize_t ioHdlcWriteTmo(iohdlc_station_peer_t *peer, const void *buf, 
                        size_t count, uint32_t timeout_ms) {
  iohdlc_station_t *s = peer->stationp;
  iohdlc_frame_t *fp;
  const uint8_t *data = (const uint8_t *)buf;
  size_t remaining = count;
  uint8_t *info_ptr;
  size_t chunk_size;
  
  /* Validate parameters */
  if (buf == NULL || count == 0) {
    s->errorno = EINVAL;
    return -1;
  }
  
  /* Check if connected */
  if (!(peer->ss_state & IOHDLC_SS_ST_CONN)) {
    s->errorno = ENOTCONN;
    return -1;
  }
  
  /* Loop to send all bytes, fragmenting if necessary */
  while (remaining > 0) {
    /* Flow control: wait if window full or pool low watermark */
    msg_t result = iohdlc_bsem_wait_timeout_ms(&peer->tx_sem, timeout_ms);
    if (result != MSG_OK) {
      s->errorno = ETIMEDOUT;
      return -1;
    }
    
    /* Inner loop: send frames while both conditions permit.
       This handles the case where N>1 slots become available (ACK burst).
       Binary semaphore signals "at least 1 slot available", but we check
       the actual condition directly to consume all available slots. */
    while (remaining > 0) {
      /* Lock to check condition and queue frame atomically */
      iohdlc_mutex_lock(&peer->state_mutex);
      
      /* Check if we can send (window not full AND pool not low) */
      if (peer->i_pending_count >= (2 * peer->ks) ||
          hdlcPoolGetState(s->frame_pool) != IOHDLC_POOL_NORMAL) {
        iohdlc_mutex_unlock(&peer->state_mutex);
        break;  /* Cannot send now, go back to outer wait */
      }
      
      /* Allocate frame from pool (quick operation with internal lock) */
      fp = hdlcTakeFrame(s->frame_pool);
      if (fp == NULL) {
        /* Pool exhausted - should not happen given watermark check above.
           Break inner loop and outer wait will re-check condition. */
        iohdlc_mutex_unlock(&peer->state_mutex);
        break;
      }
      
      /* Calculate chunk size for this frame (max mifls) */
      chunk_size = (remaining < peer->mifls) ? remaining : peer->mifls;
      
      /* Set address field */
      IOHDLC_FRAME_ADDR(s, fp) = IOHDLC_IS_PRI(s) ? peer->addr : s->addr;
      
      /* Set control field: I-frame ID and N(S) */
      IOHDLC_FRAME_CTRL(s, fp, 0) = IOHDLC_I_ID;
      IOHDLC_FRAME_SET_NS(s, fp, peer->vs);
      
      /* Increment V(S) for next frame */
      peer->vs = (peer->vs + 1) & s->modmask;
      
      /* Copy data to info field */
      info_ptr = IOHDLC_FRAME_INFO(s, fp);
      memcpy(info_ptr, data, chunk_size);
      
      /* Calculate elen: FFF + ADDR + CTRL + INFO */
      uint8_t *end = info_ptr + chunk_size;
      fp->elen = (uint16_t)(end - fp->frame);
      
      /* Valorize FFF if present */
      ioHdlcValorizeFFF(s, fp);
      
      /* Queue frame for transmission */
      ioHdlc_frameq_insert(&peer->i_trans_q, fp);
      
      /* Increment pending count for flow control */
      peer->i_pending_count++;
      
      iohdlc_mutex_unlock(&peer->state_mutex);
      
      /* Update loop variables */
      data += chunk_size;
      remaining -= chunk_size;
    }
  }
  
  /* Signal TX task that frames are ready */
  s_runner_ops->broadcast_flags(s, IOHDLC_EVT_ISNDREQ);
  
  return (ssize_t)count;
}

/**
 * @brief   Read data from peer via HDLC I-frames.
 * @details Blocks until I-frame available in reception queue, copies to buffer.
 *          Handles partial frame reads: if buffer smaller than frame, saves
 *          frame state for next read. Returns as many bytes as possible.
 *          
 * @param[in] peer       Peer descriptor
 * @param[out] buf       Buffer to receive data
 * @param[in] count      Maximum bytes to read
 * @param[in] timeout_ms Timeout in milliseconds (IOHDLC_WAIT_FOREVER for blocking)
 * 
 * @return               Bytes read on success, -1 on error
 * @retval >0            Number of bytes read
 * @retval -1            Error occurred (check station->errorno)
 * 
 * @note Blocks until frame available or timeout.
 * @note Releases frame back to pool when fully consumed (may trigger watermark).
 * @note Supports partial reads: call multiple times to consume large frames.
 */
ssize_t ioHdlcReadTmo(iohdlc_station_peer_t *peer, void *buf, 
                      size_t count, uint32_t timeout_ms) {
  iohdlc_station_t *s = peer->stationp;
  iohdlc_frame_t *fp;
  uint8_t *info_ptr;
  size_t info_len;
  size_t available_bytes;
  size_t bytes_to_copy;
  
  /* Validate parameters */
  if (buf == NULL || count == 0) {
    s->errorno = EINVAL;
    return -1;
  }
  
  /* Check if connected */
  if (!(peer->ss_state & IOHDLC_SS_ST_CONN)) {
    s->errorno = ENOTCONN;
    return -1;
  }
  
  size_t total_bytes_read = 0;
  uint8_t *dest = (uint8_t *)buf;
  
  /* Greedy consumption loop: read frames until count exhausted or queue empty.
     This improves efficiency by draining the queue in a single syscall
     instead of requiring multiple calls when multiple frames are available. */
  while (total_bytes_read < count) {
    /* Check if we have a partial frame from previous read */
    if (peer->partial_read_frame != NULL) {
      fp = peer->partial_read_frame;
    } else {
      /* Try to get next frame from queue.
         On first iteration: wait with timeout on semaphore.
         On subsequent iterations: check queue directly (semaphore is binary,
         only signaled once even if multiple frames available). */
      if (total_bytes_read == 0) {
        /* First frame: wait with timeout */
        msg_t result = iohdlc_bsem_wait_timeout_ms(&peer->i_recept_sem, timeout_ms);
        if (result != MSG_OK) {
          s->errorno = ETIMEDOUT;
          return -1;
        }
      }
      
      /* Remove frame from queue (protect with mutex) */
      iohdlc_mutex_lock(&peer->state_mutex);
      fp = NULL;
      if (!ioHdlc_frameq_isempty(&peer->i_recept_q))
        fp = ioHdlc_frameq_remove(&peer->i_recept_q);
      iohdlc_mutex_unlock(&peer->state_mutex);
      
      if (fp == NULL) {
        /* Queue empty: on first iteration shouldn't happen after semaphore wait.
           On subsequent iterations, this is expected when queue is drained. */
        if (total_bytes_read > 0)
          break;  /* Return what we've read so far */
        s->errorno = EAGAIN;
        return -1;
      }
      
      /* Start reading from beginning of this frame */
      peer->partial_read_offset = 0;
    }
    
    /* Get info field pointer and calculate total length */
    info_ptr = IOHDLC_FRAME_INFO(s, fp);
    info_len = fp->elen - (s->frame_offset + 1 + s->ctrl_size);
    
    /* Calculate available bytes from current offset */
    available_bytes = info_len - peer->partial_read_offset;
    
    /* Copy as many bytes as fit in remaining buffer space */
    size_t space_remaining = count - total_bytes_read;
    bytes_to_copy = (available_bytes < space_remaining) ? available_bytes : space_remaining;
    memcpy(dest, info_ptr + peer->partial_read_offset, bytes_to_copy);
    
    /* Update read state */
    peer->partial_read_offset += bytes_to_copy;
    dest += bytes_to_copy;
    total_bytes_read += bytes_to_copy;
    
    /* Check if frame fully consumed */
    if (peer->partial_read_offset < info_len) {
      /* Frame partially read: save for next call and exit loop */
      peer->partial_read_frame = fp;
      break;  /* Buffer full, frame partially consumed */
    }
    /* Frame fully read: release back to pool */
    hdlcReleaseFrame(s->frame_pool, fp);
    peer->partial_read_frame = NULL;
    peer->partial_read_offset = 0;
  }
  
  return (ssize_t)total_bytes_read;
}

/** @} */
