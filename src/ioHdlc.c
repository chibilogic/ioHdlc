/*
 * ioHdlc
 * Copyright (C) 2024 Isidoro Orabona
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This software is dual-licensed:
 *  - GNU General Public License v3.0 (or later)
 *  - Commercial license (available from Chibilogic s.r.l.)
 *
 * For commercial licensing inquiries:
 *   info@chibilogic.com
 *
 * See the LICENSE file for details.
 */
/**
 * @file    src/ioHdlc.c
 * @brief   HDLC Application Interface Implementation.
 * @details Implements public API functions for station link management
 *          and data transfer (LinkUp, LinkDown, Write, Read).
 *          OS-agnostic implementation using OSAL wrappers.
 *
 *          This module is the integration-facing construction layer. It
 *          validates configuration, derives frame sizing and protocol
 *          parameters, initializes shared resources, and binds the selected
 *          driver/pool/backend objects into a ready-to-run station.
 *
 * @addtogroup ioHdlc_api
 * @{
 */

#include "ioHdlc.h"
#include "ioHdlc_core.h"
#include "ioHdlc_app_events.h"
#include "ioHdlcosal.h"
#include "ioHdlcqueue.h"
#include "ioHdlclist.h"
#include "ioHdlcstreamport.h"
#include "ioHdlc_log.h"
#include <string.h>
#include <errno.h>

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
 * @brief   Calculate optimal frame buffer size based on configuration.
 * @details Computes frame size = FFF + ADDR + CTRL + INFO + FCS + CLOSING_FLAG.
 *          Respects FFF TYPE0 limit (127 bytes) and TYPE1 limit (4095 bytes).
 *          When transparency is enabled, worst-case byte-stuffing expansion is
 *          applied so the pool arena can still hold fully encoded frames.
 *
 * @param[in] log2mod       Log2 of modulus (3=mod8, 7=mod128, 15=mod32768)
 * @param[in] fff_type      FFF type: 0=none, 1=TYPE0, 2=TYPE1
 * @param[in] fcs_size      FCS size in bytes (0, 2, 4)
 * @param[in] max_info_len  Desired max INFO field length
 * @return                  Optimal frame buffer size in bytes
 */
static uint32_t calculate_frame_size(uint8_t log2mod, uint8_t fff_type,
                                     uint8_t fcs_size, uint32_t max_info_len,
                                     bool transparency) {
  /* Calculate ctrl_size from modulo */
  uint8_t ctrl_size = (log2mod == 3) ? 1 : ((log2mod + 1) / 4);
  
  /* Calculate elen (frame length without FCS and FLAGS) */
  uint32_t elen = fff_type + 1 + ctrl_size + max_info_len;
  
  /* Apply FFF type limits on total frame (elen + FCS) */
  if (fff_type != 0) {
    /* TYPE0: 127 max, TYPE1: 4095 max */
    uint32_t fff_limit = (fff_type == 1) ? 127 : 4095;
    uint32_t max_elen = fff_limit - fcs_size;
    if (elen > max_elen) {
      elen = max_elen;
    }
  }
  
  /* Buffer size needed: elen + FCS + CLOSING_FLAG */
  uint32_t frame_size = elen + fcs_size + 1;
  
  /* Apply transparency overhead if needed (worst-case 2x byte stuffing) */
  if (transparency) {
    frame_size = frame_size * 2;
  }
  
  return frame_size;
}

/*===========================================================================*/
/* Module exported functions.                                                */
/*===========================================================================*/

/**
 * @brief   Initialize HDLC station with configuration.
 * @details Initializes a station descriptor with the provided configuration:
 *          - Auto-initializes frame pool from arena
 *          - Calculates optimal frame size based on FFF type and constraints
 *          - Configures modulus parameters (modmask, pfoctet, ctrl_size)
 *          - Sets operational mode and flags
 *          - Initializes peer list and event sources
 *          - Configures optional functions (REJ, FFF, STB)
 *          - Sets TX/RX handlers based on mode
 *
 * @param[in] ioHdlcsp      Station descriptor to initialize
 * @param[in] ioHdlcsconfp  Configuration parameters
 * @return                  0 on success, -1 on error (check iohdlc_errno)
 * 
 * @note The caller must provide:
 *       - Frame arena memory (frame_arena, frame_arena_size)
 *       - Driver implementation (driver)
 *       - Optional: physical device and config (phydriver, phydriver_config)
 */
int32_t ioHdlcStationInit(iohdlc_station_t *ioHdlcsp,
                          const iohdlc_station_config_t *ioHdlcsconfp) {
  uint32_t mod2 = 0;
  uint8_t mode = ioHdlcsconfp->mode;

  if ((mode != IOHDLC_OM_NDM) && (mode != IOHDLC_OM_ADM)) {
    iohdlc_errno = EINVAL;
    return -1;
  }

  /* Validate arena parameters */
  if (ioHdlcsconfp->frame_arena == NULL || ioHdlcsconfp->frame_arena_size == 0) {
    iohdlc_errno = EINVAL;
    return -1;
  }

  /* Read port constraints from physical driver and validate against config.
     phydriver points to ioHdlcStreamPort when the sw driver is used. */
  uint32_t port_constr = 0;
  if (ioHdlcsconfp->phydriver != NULL) {
    const ioHdlcStreamPort *portp = (const ioHdlcStreamPort *)ioHdlcsconfp->phydriver;
    const iohdlc_stream_caps_t *caps =
      (portp->ops && portp->ops->get_caps) ? portp->ops->get_caps(portp->ctx) : NULL;
    port_constr = caps ? caps->constraints : 0;
    if ((port_constr & IOHDLC_PORT_CONSTR_TWA_ONLY) &&
        !(ioHdlcsconfp->flags & IOHDLC_FLG_TWA)) {
      iohdlc_errno = EINVAL;   /* Port requires TWA but config does not set FLG_TWA. */
      return -1;
    }
  }

  /* Basic station parameters */
  ioHdlcsp->addr = ioHdlcsconfp->addr;
  ioHdlcsp->flags = ioHdlcsconfp->flags;
  ioHdlcsp->mode = mode;

  /* Calculate modulus parameters */
  mod2 = ioHdlcsconfp->log2mod;
  ioHdlcsp->framing.modmask = (1U << mod2) - 1;  /* 7, 127, 32767, 2147483647 */
  ioHdlcsp->framing.pfoctet = (mod2 + 1) / 8;
  ioHdlcsp->framing.ctrl_size = (mod2 == 3) ? 1 : (ioHdlcsp->framing.pfoctet * 2);

  /* Reply timeout: use config value, or default 100ms if 0.
     In ABM/ADM, both stations may initiate SABM simultaneously. To break
     symmetry and reduce contention, add a small address-proportional skew
     so that stations with different addresses use different timeouts. */
  {
    uint32_t base_tmo = (ioHdlcsconfp->reply_timeout_ms != 0) ?
                          ioHdlcsconfp->reply_timeout_ms :
                          IOHDLC_REPLY_TIMEOUT_MS_DEFAULT;
    if (ioHdlcsconfp->mode == IOHDLC_OM_ABM ||
        ioHdlcsconfp->mode == IOHDLC_OM_ADM) {
      base_tmo += ((ioHdlcsconfp->addr - 1U) & 0xFFU) *
                  (base_tmo / IOHDLC_REPLY_TIMEOUT_ADDR_SKEW_DIVISOR);
    }
    ioHdlcsp->reply_timeout_ms = base_tmo;
  }

  /* Poll retry max: store config value for later use when adding peers */
  ioHdlcsp->poll_retry_max_cfg = (ioHdlcsconfp->poll_retry_max != 0) ?
                                  ioHdlcsconfp->poll_retry_max :
                                  IOHDLC_POLL_RETRY_MAX_DEFAULT;

  /* Store port constraints for later checks (e.g. at link-up). */
  ioHdlcsp->port_constraints = port_constr;

  /* Driver setup */
  ioHdlcsp->driver = ioHdlcsconfp->driver;
  
  /* Validate driver is present - station cannot operate without driver */
  if (ioHdlcsp->driver == NULL || ioHdlcsp->driver->vmt == NULL) {
    iohdlc_errno = EINVAL;
    return -1;
  }

  /* Initialize peer list */
  ioHdlc_peerl_init(&ioHdlcsp->peers);
  ioHdlcsp->c_peer = NULL;
  ioHdlcsp->connected_count = 0;

  /* Initialize event sources (OS-agnostic via osal) */
  iohdlc_evt_init(&ioHdlcsp->cm_es);
  iohdlc_evt_init(&ioHdlcsp->app_es);

  /* Initialize runner state */
  ioHdlcsp->stop_requested = false;
  ioHdlcsp->driver_started = false;
  ioHdlcsp->runner_started = false;
  ioHdlcsp->runner_context = NULL;

  /* Set TX/RX handlers based on mode */
  if (mode == IOHDLC_OM_NDM) {
    ioHdlcsp->tx_fn = ioHdlcNrmTx;
    ioHdlcsp->rx_fn = ioHdlcNrmRx;
  } else {
    ioHdlcsp->tx_fn = ioHdlcAbmTx;
    ioHdlcsp->rx_fn = ioHdlcAbmRx;
  }

  /* Configure optional functions */
  if (ioHdlcsconfp->optfuncs != NULL) {
    /* User provided custom optional functions */
    memcpy(ioHdlcsp->optfuncs, ioHdlcsconfp->optfuncs, sizeof ioHdlcsp->optfuncs);
  } else {
    /* Use default optional functions: REJ, SST, FFF enabled */
    memset(ioHdlcsp->optfuncs, 0, sizeof ioHdlcsp->optfuncs);
    ioHdlcsp->optfuncs[IOHDLC_OPT_SST_OCT] |= IOHDLC_OPT_SST;
    ioHdlcsp->optfuncs[IOHDLC_OPT_REJ_OCT] |= IOHDLC_OPT_REJ;
    ioHdlcsp->optfuncs[IOHDLC_OPT_FFF_OCT] |= IOHDLC_OPT_FFF;
    ioHdlcsp->optfuncs[IOHDLC_OPT_INH_OCT] |= IOHDLC_OPT_INH;
    
  }

  /* Initialize fast-access critical flags from optfuncs */
  ioHdlcsp->flags_critical = 0;
  
  if (ioHdlcsp->optfuncs[IOHDLC_OPT_FFF_OCT] & IOHDLC_OPT_FFF) {
    ioHdlcsp->flags_critical |= IOHDLC_CFLG_FFF;
    ioHdlcsp->framing.frame_offset = 1;  /* FFF TYPE 0 present: addr starts at offset 1 */
  } else {
    ioHdlcsp->framing.frame_offset = 0;  /* No FFF: addr starts at offset 0 */
  }
  
  if (ioHdlcsp->optfuncs[IOHDLC_OPT_REJ_OCT] & IOHDLC_OPT_REJ) {
    ioHdlcsp->flags_critical |= IOHDLC_CFLG_REJ;
  }
  
  if (ioHdlcsp->optfuncs[IOHDLC_OPT_STB_OCT] & IOHDLC_OPT_STB) {
    ioHdlcsp->flags_critical |= IOHDLC_CFLG_STB;
  }

  /* Configure driver settings */
  /* Extract station options */
  bool want_transparency = (ioHdlcsp->optfuncs[IOHDLC_OPT_STB_OCT] & IOHDLC_OPT_STB) != 0;
  bool inh_precedence = (ioHdlcsp->optfuncs[IOHDLC_OPT_INH_OCT] & IOHDLC_OPT_INH) != 0;
  
  /* FFF and Transparency are mutually exclusive */
  if (ioHdlcsp->framing.frame_offset && want_transparency) {
    if (inh_precedence) {
      want_transparency = false;  /* FFF takes precedence (INH option) */
    } else {
      iohdlc_errno = EINVAL; 
      return -1;  /* Conflicting options */
    }
  }
  
  /* Query driver capabilities */
  const ioHdlcDriverCapabilities *caps = hdlcGetCapabilities(ioHdlcsp->driver);
  
  /* Validate transparency against driver capabilities */
  if (want_transparency && !caps->transparency.hw_support && 
      !caps->transparency.sw_available) {
    iohdlc_errno = ENOTSUP;
    return -1;  /* Driver doesn't support transparency */
  }
    
  /* Determine FFF type for driver and frame size calculation */
  uint8_t fff_type = ioHdlcsconfp->fff_type;
  if (fff_type == 0) {
    /* Auto-detect from optfuncs */
    if (ioHdlcsp->framing.frame_offset != 0) {
      /* FFF enabled: check for TYPE1 flag (future extension) */
      /* For now, default to TYPE0 when FFF enabled */
      fff_type = 1;  /* TYPE0 */
    }
    /* else fff_type stays 0 (no FFF) */
  }
  
  /* Update frame_offset based on final fff_type (may differ from optfuncs) */
  ioHdlcsp->framing.frame_offset = fff_type;  /* 0, 1, or 2 bytes */
  
  /* Configure driver with validated settings */
  /* Select FCS size (default: 16-bit per ISO 13239) */
  int32_t config_result = hdlcConfigure(ioHdlcsp->driver, caps->fcs.default_size, 
                                        want_transparency, fff_type);
  if (config_result != 0) {
    iohdlc_errno = config_result;
    return -1;  /* errno-compatible error */
  }
  
  /* Store FCS size for overhead calculation */
  ioHdlcsp->fcs_size = caps->fcs.default_size;
  
  /* Determine max_info_len for frame size calculation */
  uint32_t max_info = ioHdlcsconfp->max_info_len;
  if (max_info == 0) {
    /* Auto: optimal default based on FFF type */
    uint8_t ctrl_size = ioHdlcsp->framing.ctrl_size;
    if (fff_type == 1) {
      /* TYPE0: 127 - FFF(1) - ADDR(1) - CTRL - FCS */
      max_info = 127 - 1 - 1 - ctrl_size - caps->fcs.default_size;
    } else if (fff_type == 2) {
      /* TYPE1: 4095 - FFF(2) - ADDR(1) - CTRL - FCS */
      max_info = 4095 - 2 - 1 - ctrl_size - caps->fcs.default_size;
    } else {
      /* No FFF: use the configured default INFO budget. */
      max_info = IOHDLC_MAX_INFO_LEN_DEFAULT_NO_FFF;
    }
  }
  
  /* Calculate optimal frame buffer size */
  uint32_t frame_size = calculate_frame_size(ioHdlcsconfp->log2mod, 
                                              fff_type,
                                              caps->fcs.default_size, 
                                              max_info,
                                              want_transparency);
  
  /* Calculate the low watermark percentage. */
  uint32_t num_frames = ioHdlcsconfp->frame_arena_size / frame_size;
  uint8_t watermark_pct = (ioHdlcsconfp->pool_watermark != 0) ? 
                          ioHdlcsconfp->pool_watermark :
                          IOHDLC_POOL_WATERMARK_PCT_DEFAULT;
  
  /* Validate arena has reasonable size */
  if (num_frames < IOHDLC_MIN_FRAME_POOL_FRAMES) {
    iohdlc_errno = ENOMEM;
    return -1;  /* Arena too small */
  }
  
  /* Initialize frame pool directly in station storage */
  fmpInit(&ioHdlcsp->frame_pool, 
          ioHdlcsconfp->frame_arena,
          ioHdlcsconfp->frame_arena_size,
          frame_size,
          IOHDLC_FRAME_POOL_ALIGNMENT);
  
  /* Configure low/high watermarks with the configured hysteresis multiplier. */
  hdlcPoolConfigWatermark((ioHdlcFramePool *)&ioHdlcsp->frame_pool, 
                          watermark_pct,
                          watermark_pct * IOHDLC_POOL_WATERMARK_HIGH_MULTIPLIER,
                          NULL,
                          NULL, NULL);

  /* Start driver if physical device provided */
  if (ioHdlcsconfp->phydriver != NULL && ioHdlcsp->driver != NULL) {
    hdlcStart(ioHdlcsp->driver,
              ioHdlcsconfp->phydriver,
              ioHdlcsconfp->phydriver_config,
              (ioHdlcFramePool *)&ioHdlcsp->frame_pool);
    ioHdlcsp->driver_started = true;
  }

  iohdlc_errno = 0;
  return 0;
}

/**
 * @brief   Tear down a station and stop its runtime components.
 * @details Force-stops the runner if active and stops the associated driver.
 *          Safe to call multiple times and after partial cleanup paths.
 * @param[in] ioHdlcsp    station descriptor
 * @return                0 on success, -1 on invalid argument
 */
int32_t ioHdlcStationDeinit(iohdlc_station_t *ioHdlcsp) {
  if (ioHdlcsp == NULL) {
    iohdlc_errno = EINVAL;
    return -1;
  }

  if (ioHdlcsp->runner_started || ioHdlcsp->runner_context != NULL) {
    (void)ioHdlcRunnerStop(ioHdlcsp);
  }

  if (ioHdlcsp->driver != NULL) {
    hdlcStop(ioHdlcsp->driver);
  }

  ioHdlcsp->stop_requested = false;
  ioHdlcsp->driver_started = false;
  ioHdlcsp->runner_started = false;
  ioHdlcsp->runner_context = NULL;
  iohdlc_errno = 0;
  return 0;
}

/**
 * @brief   Look up a peer by address.
 * @param[in] ioHdlcsp    station descriptor.
 * @param[in] peer_addr   peer protocol address.
 * @return                matching peer descriptor, or NULL if not found.
 */
iohdlc_station_peer_t *ioHdlcAddr2peer(iohdlc_station_t *s, uint32_t peer_addr) {
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
 * @note iohdlc_errno field contains detailed error code on failure.
 * @note mifl is calculated as: framesize - (FFF + ADDR + CTRL + FCS)
 *       For modulo 8: framesize - (1 + 1 + 1 + 2) = framesize - 5 (if FFF enabled)
 * 
 */
int32_t ioHdlcAddPeer(iohdlc_station_t *s, iohdlc_station_peer_t *peer,
                      uint32_t addr) {
  /* Secondary stations can only add peer when in disconnected mode.
     ABM combined stations are not subject to this restriction. */
  if (IOHDLC_IS_SEC(s) &&
      (s->mode != IOHDLC_OM_NDM) && (s->mode != IOHDLC_OM_ADM)) {
    iohdlc_errno = EINVAL;
    return -1;
  }

  /* Check that addr does not already exist */
  if (ioHdlcAddr2peer(s, addr) != NULL) {
    iohdlc_errno = EEXIST;
    return -1;
  }

  /* Calculate mifl from frame pool size minus overhead.
     Overhead = FFF (frame_offset) + ADDR (1) + CTRL (ctrl_size) + FCS (fcs_size) + FLAG (1) */
  uint32_t overhead = s->framing.frame_offset + 1 + s->framing.ctrl_size + s->fcs_size + 1;
  uint32_t mifl = (s->frame_pool.framesize > overhead) ? 
                  (s->frame_pool.framesize - overhead) :
                  IOHDLC_PEER_MIFL_FALLBACK;

  /* Initialize the peer structure */
  memset(peer, 0, sizeof *peer);
  peer->addr = addr;
  peer->stationp = s;
  peer->kr = peer->ks = s->framing.modmask;
  peer->miflr = peer->mifls = mifl;
  peer->poll_retry_max = s->poll_retry_max_cfg;
  
  /* Initialize queues */
  ioHdlc_frameq_init(&peer->i_recept_q);
  ioHdlc_frameq_init(&peer->i_retrans_q);
  ioHdlc_frameq_init(&peer->i_trans_q);
  
  /* Initialize flow control condvar, semaphore and mutex */
  iohdlc_condvar_init(&peer->tx_cv);            /* TX flow control (used with state_mutex) */
  iohdlc_sem_init(&peer->i_recept_sem, 0);      /* Counting semaphore - number of frames available */
  iohdlc_mutex_init(&peer->state_mutex);        /* Mutex for state */
  
  /* Initialize virtual timers (reply and I-frame reply) */
  iohdlc_vt_init(&peer->reply_tmr, &s->cm_es, IOHDLC_EVT_C_RPLYTMO);
  iohdlc_vt_init(&peer->t3_tmr, &s->cm_es, IOHDLC_EVT_T3_TMO);
  
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

/**
 * @brief   Set the transmit and receive window size for a peer.
 * @details Must be called after @p ioHdlcAddPeer() and before
 *          @p ioHdlcRunnerStart().
 *
 * @param[in] peer  Peer descriptor (already added to a station)
 * @param[in] ks    Transmit window size (1..modmask)
 * @param[in] kr    Receive window size (1..modmask)
 *
 * @return              0 on success, -1 on error
 * @retval 0            Window size successfully applied
 * @retval -1           Error occurred:
 *                      - EINVAL: ks or kr is 0 or exceeds the station's modmask
 *
 * @note iohdlc_errno field contains detailed error code on failure.
 */
int32_t ioHdlcPeerSetWindow(iohdlc_station_peer_t *peer, uint32_t ks, uint32_t kr) {
  if (ks == 0 || ks > peer->stationp->framing.modmask ||
      kr == 0 || kr > peer->stationp->framing.modmask) {
    iohdlc_errno = EINVAL;
    return -1;
  }
  peer->ks = ks;
  peer->kr = kr;
  return 0;
}

/**
 * @brief   Establish data link connection with a peer (extended version).
 * @details Initiates connection by sending appropriate U-frame command
 *          (SNRM/SARM/SABM) and waiting for UA response. Implements
 *          retry logic with configurable timeout.
 *          
 *          Primary station behavior:
 *          - Sends set-mode command with P=1
 *          - Waits for UA response with F=1
 *          - Retries up to IOHDLC_LINKUP_MAX_RETRIES on timeout
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
 * @note iohdlc_errno field contains detailed error code on failure.
 * @note This function blocks until connection completes or fails.
 * @note Uses protocol-level retry (no application timeout parameter).
 * @note Uses app_es event source to avoid conflicts with core events.
 * 
 */
int32_t ioHdlcStationLinkUpEx(iohdlc_station_t *s, uint32_t peer_addr, 
                              uint8_t mode, eventmask_t evt_mask) {
  iohdlc_station_peer_t *p;
  iohdlc_event_listener_t listener;
  uint8_t u_cmd, s_mode = 0;
  uint32_t retry_count;

  /* Find peer by address */
  p = ioHdlcAddr2peer(s, peer_addr);
  if (p == NULL) {
    iohdlc_errno = EINVAL;
    return -1;
  }

  /* Check if already connected */
  if (p->ss_state & IOHDLC_SS_ST_CONN) {
    iohdlc_errno = EISCONN;
    return -1;
  }

  /* Secondary stations wait for primary to initiate connection.
     ABM combined stations can initiate from either side. */
  if (IOHDLC_IS_SEC(s) && (mode != IOHDLC_OM_ABM))
    return 0;

  /* In ABM mode, there are no primary or secondary stations.
     Both the stations are equal. Set it as primary.*/
  if (mode == IOHDLC_OM_ABM) {
    s->flags |= IOHDLC_FLG_PRI;
  }

  /* Check port constraints against requested mode. */
  if ((s->port_constraints & IOHDLC_PORT_CONSTR_NRM_ONLY) &&
      (mode == IOHDLC_OM_ABM)) {
    iohdlc_errno = ENOTSUP;  /* Port does not support ABM. */
    return -1;
  }

  /* Validate mode and get corresponding U-frame command */
  u_cmd = IOHDLC_MODE_TO_UCMD(mode);
  if (u_cmd == 0) {
    iohdlc_errno = EINVAL;
    return -1;
  }

  /* Register listener on app_es for link events */
  iohdlc_evt_register(&s->app_es, &listener, evt_mask,
                      IOHDLC_APP_LINK_UP | IOHDLC_APP_LINK_REFUSED);

  /* Set mode for this connection attempt. In multipoint, all peers use
     the same mode. Write only when transitioning from disconnected state
     to avoid a data race with concurrent LinkUpEx calls. */
  if (IOHDLC_IS_DISC(s)) {
    s_mode = s->mode;
    s->mode = mode;
  }
  
  /* Connection retry loop */
  for (retry_count = 0; retry_count < IOHDLC_LINKUP_MAX_RETRIES; retry_count++) {
    /* Set unnumbered command in peer descriptor */
    p->um_cmd = u_cmd;

    /* Clear disconnected-mode flag (we're attempting connection) */
    p->ss_state &= ~IOHDLC_SS_ST_DISM;

    /* Signal TX task to send the command */
    ioHdlcBroadcastFlags(s, IOHDLC_EVT_LINK_REQ);

    /* Wait for app event with timeout.
       Multiple threads may call LinkUp on different peers concurrently.
       Verify that the event is for our peer before accepting it;
       if not, re-wait with the same timeout. */
    uint32_t timeout_ms = s->reply_timeout_ms * p->poll_retry_max;
    for (;;) {
      eventmask_t evt = iohdlc_evt_wait_any_timeout(evt_mask, timeout_ms);
      if (evt == 0) break;  /* Timeout: fall through to retry */

      eventflags_t flags = iohdlc_evt_get_and_clear_flags(&listener);

      if ((flags & IOHDLC_APP_LINK_UP) &&
          (p->ss_state & IOHDLC_SS_ST_CONN)) {
        /* Our peer is connected. */
        iohdlc_evt_unregister(&s->app_es, &listener);
        return 0;
      }
      if ((flags & IOHDLC_APP_LINK_REFUSED) && IOHDLC_PEER_DISC(p)) {
        /* Our peer was refused. */
        iohdlc_evt_unregister(&s->app_es, &listener);
        s->mode = s_mode;
        iohdlc_errno = ECONNREFUSED;
        return -1;
      }
      /* Event was for another peer: re-wait. */
    }
    /* Timeout: retry */
  }

  /* All retries exhausted */
  iohdlc_evt_unregister(&s->app_es, &listener);
  s->mode = s_mode;
  iohdlc_errno = ETIMEDOUT;
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
 *          - Retries up to IOHDLC_LINKDOWN_MAX_RETRIES on timeout
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
 * @note iohdlc_errno field contains detailed error code on failure.
 * @note This function blocks until disconnection completes or fails.
 * @note Peer state is reset (queues cleared, variables reset) on success.
 * @note Uses app_es event source to avoid conflicts with core events.
 * 
 */
int32_t ioHdlcStationLinkDownEx(iohdlc_station_t *s, uint32_t peer_addr,
                                eventmask_t evt_mask) {
  iohdlc_station_peer_t *p;
  iohdlc_event_listener_t listener;
  uint32_t retry_count;

  /* Find peer by address */
  p = ioHdlcAddr2peer(s, peer_addr);
  if (p == NULL) {
    iohdlc_errno = ENOTCONN;
    return -1;
  }

  /* Check if already disconnected */
  if (!(p->ss_state & IOHDLC_SS_ST_CONN)) {
    iohdlc_errno = ENOTCONN;
    return -1;
  }

  /* Secondary stations wait for primary to disconnect.
     ABM combined stations can disconnect from either side. */
  if (IOHDLC_IS_SEC(s) && !IOHDLC_IS_ABM(s))
    return 0;

  /* Register listener on app_es for link events */
  iohdlc_evt_register(&s->app_es, &listener, evt_mask,
                      IOHDLC_APP_LINK_DOWN);

  /* Disconnection retry loop */
  for (retry_count = 0; retry_count < IOHDLC_LINKDOWN_MAX_RETRIES; retry_count++) {
    /* Set DISC command in peer descriptor */
    p->um_cmd = IOHDLC_U_DISC;

    /* Clear disconnected-mode flag (not yet confirmed) */
    p->ss_state &= ~IOHDLC_SS_ST_DISM;

    /* Signal TX task to send DISC */
    ioHdlcBroadcastFlags(s, IOHDLC_EVT_LINK_REQ);

    /* Wait for app event with timeout.
       Verify that the event is for our peer before accepting it. */
    uint32_t timeout_ms = s->reply_timeout_ms * p->poll_retry_max;
    for (;;) {
      eventmask_t evt = iohdlc_evt_wait_any_timeout(evt_mask, timeout_ms);
      if (evt == 0) break;  /* Timeout: fall through to retry */

      eventflags_t flags = iohdlc_evt_get_and_clear_flags(&listener);

      if ((flags & IOHDLC_APP_LINK_DOWN) &&
          !(p->ss_state & IOHDLC_SS_ST_CONN)) {
        /* Our peer is disconnected. */
        iohdlc_evt_unregister(&s->app_es, &listener);
        return 0;
      }
      /* Event was for another peer: re-wait. */
    }
    /* Timeout: retry */
  }

  /* All retries exhausted */
  iohdlc_evt_unregister(&s->app_es, &listener);
  iohdlc_errno = ETIMEDOUT;
  return -1;
}

/**
 * @brief   Flow control: writer wait condition is true
 *          until exceeding pending frames exist OR pool low
 */
static inline uint32_t writer_pending_limit(const iohdlc_station_peer_t *p) {
  uint32_t margin = p->ks / IOHDLC_WRITER_PENDING_MARGIN_DIVISOR;
  return p->ks + ((margin < IOHDLC_WRITER_PENDING_MARGIN_MIN) ?
                   IOHDLC_WRITER_PENDING_MARGIN_MIN : margin);
}

#define W_WAIT_COND(s, p) \
	          (p->i_pending_count >= writer_pending_limit(p) || \
	          hdlcPoolGetState(&s->frame_pool) != IOHDLC_POOL_NORMAL)
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
 * @retval -1            Error occurred (check iohdlc_errno)
 * 
 * @note Blocks if i_pending_count >= 2*ks OR pool is LOW_WATER.
 * @note Sets address, N(S), frame ID; N(R) and P/F set during TX.
 * @note Automatically fragments data if count > mifls.
 * @note Multiple threads can call Write concurrently on same peer.
 *       Flow control is thread-safe, but frame transmission ORDER is not guaranteed
 *       between concurrent writers. If ordering matters, serialize writes externally.
 * 
 */
ssize_t ioHdlcWriteTmo(iohdlc_station_peer_t *peer, const void *buf, 
                        size_t count, uint32_t timeout_ms) {
  iohdlc_station_t *s = peer->stationp;
  iohdlc_frame_t *fp;
  const uint8_t *data = (const uint8_t *)buf;
  size_t remaining = count;
  uint8_t *info_ptr;
  size_t chunk_size;
  bool signal_tx = false;
  
  /* Validate parameters */
  if (buf == NULL || count == 0) {
    iohdlc_errno = EINVAL;
    return -1;
  }
  
  /* Check if connected */
  if (IOHDLC_PEER_DISC(peer)) {
    iohdlc_errno = ENOTCONN;
    return -1;
  }
  
  /* Loop to send all bytes, fragmenting if necessary.
     Uses condition variable for flow control: wait on actual condition
     (window full OR pool low).
     Coalesce IOHDLC_EVT_TX_IFRM_ENQ signals. */
  while (remaining > 0) {
    /* Acquire mutex for condition check and frame construction */
    iohdlc_mutex_lock(&peer->state_mutex);
    
    /* Flow control: wait while exceeding pending frames exist OR pool low.
       Condition variable automatically releases mutex during wait
       and re-acquires it before returning. */
    while (!IOHDLC_PEER_DISC(peer) && W_WAIT_COND(s, peer)) {
      if (signal_tx) {
        /* Signal TX task that frames are ready */
        ioHdlcBroadcastFlags(s, IOHDLC_EVT_TX_IFRM_ENQ);
        signal_tx = false;  /* Avoid redundant signals while waiting */
      }
      msg_t result = iohdlc_condvar_wait_timeout(&peer->tx_cv,
                                                  &peer->state_mutex,
                                                  timeout_ms);
      if ((result == MSG_TIMEOUT) && W_WAIT_COND(s, peer)) {
         /* Timeout occurred and condition still not satisfied */
        iohdlc_errno = ETIMEDOUT;
        ssize_t t = count -remaining;
        return t != 0 ? t : -1;  /* Return bytes written so far */
      }
      if (result == MSG_TIMEOUT)
        iohdlc_mutex_lock(&peer->state_mutex);
    }

    if (IOHDLC_PEER_DISC(peer)) {
        iohdlc_mutex_unlock(&peer->state_mutex);
        return count - remaining;  /* Return bytes written so far */
    }
    
    /* Condition satisfied: window has space AND pool is normal.
       Calculate chunk size for this frame (max mifls). */
    chunk_size = (remaining < peer->mifls) ? remaining : peer->mifls;
    
    /* Avoid creating frames with FFF == FLAG (0x7E)
       when FFF present and chunk_size > 1 */
    if (s->framing.frame_offset != 0 && chunk_size > 1) {
      uint32_t frame_total = s->framing.frame_offset + 1 + s->framing.ctrl_size + chunk_size + s->fcs_size;
      if ((frame_total & 0xFF) == 0x7E) {
         chunk_size = chunk_size / 2;
      }
    }

    /* Allocate frame from pool */
    fp = hdlcTakeFrame(&s->frame_pool);
    iohdlc_mutex_unlock(&peer->state_mutex);
    if (fp == NULL) {
      /* Pool exhausted - this could still happen due to low-level driver
         read activity */
      continue;
    }
    
    /* Build frame outside mutex (no shared state accessed) */
    
    /* Set address field: commands use peer address, responses use station address.
       In ABM, the final address is overwritten at TX time based on the
       command/response decision (see ioHdlcNrmTx I-frame loop). */
    IOHDLC_FRAME_ADDR(s, fp) = IOHDLC_IS_PRI(s) ? peer->addr : s->addr;
    
    /* Set control field: I-frame ID */
    IOHDLC_FRAME_CTRL(s, fp, 0) = IOHDLC_I_ID;
    
    /* Copy data to info field */
    info_ptr = IOHDLC_FRAME_INFO(s, fp);
    memcpy(info_ptr, data, chunk_size);
    
    /* Calculate elen: FFF + ADDR + CTRL + INFO */
    uint8_t *end = info_ptr + chunk_size;
    fp->elen = (uint16_t)(end - fp->frame);
    
    /* FFF will be valorized by driver */
    
    /* Re-acquire mutex to enqueue frame and update state */
    iohdlc_mutex_lock(&peer->state_mutex);
    
    /* Queue frame for transmission */
    ioHdlc_frameq_insert(&peer->i_trans_q, &fp->q);
    signal_tx = true;

    /* Increment pending count for flow control */
    peer->i_pending_count++;
    
    iohdlc_mutex_unlock(&peer->state_mutex);
    
    /* Update loop variables */
    data += chunk_size;
    remaining -= chunk_size;
  }
  
  /* Signal TX task that frames are ready */
  ioHdlcBroadcastFlags(s, IOHDLC_EVT_TX_IFRM_ENQ);
  
  return (ssize_t)count;
}

/**
 * @brief   Read data from peer via HDLC I-frames.
 * @details Blocks until I-frame available in reception queue, copies to buffer.
 *          Handles partial frame reads: if buffer smaller than frame, saves
 *          frame state for next read. Returns as many bytes as possible.
 *          Gracefully handle reads that occur while the peer is disconnecting.
 *          
 * @param[in] peer       Peer descriptor
 * @param[out] buf       Buffer to receive data
 * @param[in] count      Maximum bytes to read
 * @param[in] timeout_ms Timeout in milliseconds (IOHDLC_WAIT_FOREVER for blocking)
 * 
 * @return               Bytes read on success, -1 on error
 * @retval >0            Number of bytes read
 * @retval -1            Error occurred (check iohdlc_errno)
 * 
 * @note Blocks until frame available or timeout.
 * @note Releases frame back to pool when fully consumed (may trigger watermark).
 * @note Supports partial reads: call multiple times to consume large frames.
 * @note Multiple threads can call Read concurrently on same peer.
 *       Partial read state (partial_read_frame/offset) protected by state_mutex.
 *       Frames delivered in-order as received from peer.
 * 
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
    iohdlc_errno = EINVAL;
    return -1;
  }
  
  /* Check if connected */
  if (IOHDLC_PEER_DISC(peer) && ioHdlc_frameq_isempty(&peer->i_recept_q)) {
    iohdlc_errno = ENOTCONN;
    return -1;
  }
  
  ssize_t total_bytes_read = 0;
  uint8_t *dest = (uint8_t *)buf;
  
  /* Calculate absolute timeout for total operation (handle infinite timeout) */
  uint32_t start_time_ms = iohdlc_time_now_ms();
  uint32_t deadline_ms = (timeout_ms == IOHDLC_WAIT_FOREVER) ? 
                         IOHDLC_WAIT_FOREVER : (start_time_ms + timeout_ms);
  
  iohdlc_mutex_lock(&peer->state_mutex);
  ioHdlcBroadcastFlags(s, IOHDLC_EVT_PF_RECVD);
  peer->ss_state |= IOHDLC_SS_RECVING;  /* In receiving I-frames from the peer. */
  IOHDLC_SET_NEED_P(s, peer);
  iohdlc_mutex_unlock(&peer->state_mutex);
  
  /* Greedy consumption loop: read frames until count satisfied, timeout, or queue empty.
     POSIX semantics: returns bytes read even on timeout (only -1 if no bytes read yet).
     All access to partial_read_frame/offset is protected by state_mutex for thread-safety. */
  while (total_bytes_read < (ssize_t)count) {

    /* Check if we have a partial frame from previous read (mutex protected) */
    iohdlc_mutex_lock(&peer->state_mutex);
    fp = peer->partial_read_frame;
    iohdlc_mutex_unlock(&peer->state_mutex);
    
    if (fp != NULL) {
      /* Continue reading from partial frame - offset handled atomically later */
    } else {
      if (IOHDLC_PEER_DISC(peer) && ioHdlc_frameq_isempty(&peer->i_recept_q))
        break;

      /* Calculate remaining timeout (infinite if deadline is infinite) */
      uint32_t remaining_ms;
      if (deadline_ms == IOHDLC_WAIT_FOREVER) {
        remaining_ms = IOHDLC_WAIT_FOREVER;
      } else {
        uint32_t now_ms = iohdlc_time_now_ms();
        remaining_ms = (now_ms < deadline_ms) ? (deadline_ms - now_ms) : 0;
      }
      
      /* Wait for next frame with remaining timeout (counting semaphore) */
      if (iohdlc_sem_wait_timeout(&peer->i_recept_sem, remaining_ms) != MSG_OK) {
        /* Timeout: return bytes read so far, or -1 if nothing read yet */
        if (total_bytes_read > 0) {
          break;  /* POSIX: return partial read on timeout */
        }
        iohdlc_errno = ETIMEDOUT;
        total_bytes_read = -1;
        break;
      }
      
      /* Remove frame from queue (protect with mutex) */
      iohdlc_mutex_lock(&peer->state_mutex);
      fp = NULL;
      if (!ioHdlc_frameq_isempty(&peer->i_recept_q)) {
        iohdlc_frame_q_t *qh = ioHdlc_frameq_remove(&peer->i_recept_q);
        fp = IOHDLC_FRAME_FROM_Q(qh);
      }
      iohdlc_mutex_unlock(&peer->state_mutex);
      
      if (fp == NULL) {
        /* Semaphore signaled but queue empty: peer disconnected.
           If no, handle gracefully */
        if (IOHDLC_PEER_DISC(peer))
          break;
        if (total_bytes_read > 0)
          break;  /* Return what we've read so far */
        iohdlc_errno = EAGAIN;
        total_bytes_read = -1;
        break;
      }
      
      /* Start reading from beginning of this frame */
      iohdlc_mutex_lock(&peer->state_mutex);
      peer->partial_read_offset = 0;
      iohdlc_mutex_unlock(&peer->state_mutex);
    }
    
    /* Get info field pointer and calculate total length */
    info_ptr = IOHDLC_FRAME_INFO(s, fp);
    info_len = fp->elen - (s->framing.frame_offset + 1 + s->framing.ctrl_size);
    
    /* Lock mutex for entire read-update-check sequence to ensure atomicity */
    iohdlc_mutex_lock(&peer->state_mutex);
    
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
    
    /* Check if frame fully consumed (still holding mutex from line 850) */
    if (peer->partial_read_offset < info_len) {
      /* Frame partially read: save for next call and exit loop */
      peer->partial_read_frame = fp;
      iohdlc_mutex_unlock(&peer->state_mutex);
      break;  /* Buffer full, frame partially consumed */
    }
    /* Frame fully read: release back to pool (still holding mutex).
       Mutex held during release to ensure framepool callback
       (on_normal) executes with proper synchronization for tx_cv broadcast. */
    hdlcReleaseFrame(&s->frame_pool, fp);
    peer->partial_read_frame = NULL;
    peer->partial_read_offset = 0;
    
    /* Check if pool returned to normal if we are busy.
       Generate event to wake TX thread so it can send RR. */
    if (IOHDLC_IS_BUSY(s) && 
        hdlcPoolGetState(&s->frame_pool) == IOHDLC_POOL_NORMAL) {
      ioHdlcBroadcastFlags(s, IOHDLC_EVT_POOL_ST_CHG);
    }
    
    iohdlc_mutex_unlock(&peer->state_mutex);
  }
  iohdlc_mutex_lock(&peer->state_mutex);
  peer->ss_state &= ~IOHDLC_SS_RECVING;  /* Done receiving I-frames from the peer. */
  iohdlc_mutex_unlock(&peer->state_mutex);
  
  return total_bytes_read;
}

/** @} */
