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
#include "ioHdlcdma.h"
#include "ioHdlcqueue.h"
#include "ioHdlclist.h"
#include "ioHdlcfmempool_layout.h"
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
#ifndef ECONNRESET
#define ECONNRESET      104  /* Connection reset */
#endif
#ifndef EBUSY
#define EBUSY           16   /* Device or resource busy */
#endif
#ifndef EMSGSIZE
#define EMSGSIZE        90   /* Message too long */
#endif
#ifndef EIO
#define EIO             5    /* I/O error */
#endif

#define IOHDLC_SSIZE_MAX  (((size_t)-1) >> 1)

/*===========================================================================*/
/* Module exported variables.                                                */
/*===========================================================================*/

/*===========================================================================*/
/* Module local variables and types.                                         */
/*===========================================================================*/

typedef struct {
  uint32_t start_ms;
  uint32_t timeout_ms;
} iohdlc_api_timeout_t;

/*===========================================================================*/
/* Module local functions.                                                   */
/*===========================================================================*/

static inline uint32_t s_api_timeout_remaining(const iohdlc_api_timeout_t *timeoutp) {
  uint32_t elapsed_ms;

  if (timeoutp->timeout_ms == IOHDLC_WAIT_FOREVER)
    return IOHDLC_WAIT_FOREVER;

  elapsed_ms = iohdlc_time_now_ms() - timeoutp->start_ms;
  return elapsed_ms < timeoutp->timeout_ms ?
         timeoutp->timeout_ms - elapsed_ms : 0U;
}

/**
 * @brief   Derive the API safety timeout for a link-management transaction.
 * @details The protocol core uses exponential T1 backoff for normal retry
 *          windows, then a bounded final window after the last retry. The
 *          public API waits one extra base T1 slot as a safety margin.
 *          Saturates on overflow.
 * @param[in] s   Station descriptor.
 * @param[in] p   Peer descriptor.
 * @return  Safety timeout in milliseconds.
 */
static uint32_t s_link_api_timeout_ms(const iohdlc_station_t *s,
                                      const iohdlc_station_peer_t *p) {
  uint32_t t1_ms;
  uint8_t n2;
  uint64_t total;
  uint64_t last;

  IOHDLC_ASSERT(s != NULL, "s_link_api_timeout_ms: null station");
  IOHDLC_ASSERT(p != NULL, "s_link_api_timeout_ms: null peer");

  t1_ms = s->reply_timeout_ms;
  n2 = p->poll_retry_max;
  if (n2 >= 32U)
    return ~(uint32_t)0U;

  total = (uint64_t)t1_ms * (((uint64_t)1U << n2) - 1U);
  last = (uint64_t)t1_ms * IOHDLC_LAST_RETRY_T1_RATIO;
  if (last < IOHDLC_LAST_RETRY_TIMEOUT_MIN_MS)
    last = IOHDLC_LAST_RETRY_TIMEOUT_MIN_MS;

  /* Include an extra T1-sized margin for TX-thread scheduling before the
     first command actually arms the protocol reply timer. */
  total += last + ((uint64_t)t1_ms * 2U);
  if (total > ~(uint32_t)0U)
    return ~(uint32_t)0U;

  return (uint32_t)total;
}

static bool s_test_initiator_allowed(const iohdlc_station_t *s) {
  if (IOHDLC_IS_NRM(s) || IOHDLC_IS_NDM(s))
    return IOHDLC_IS_PRI(s);

  return IOHDLC_IS_ABM(s) || IOHDLC_IS_ADM(s);
}

static uint8_t s_test_pattern_byte(size_t index) {
  return (uint8_t)(0xA5U ^ (uint8_t)(index * 0x3DU) ^
                   (uint8_t)(index >> 8));
}

static void s_fill_test_pattern(uint8_t *buf, size_t len) {
  size_t i;

  for (i = 0; i < len; ++i)
    buf[i] = s_test_pattern_byte(i);
}

static bool s_test_pattern_matches(const uint8_t *buf, size_t len) {
  size_t i;

  for (i = 0; i < len; ++i) {
    if (buf[i] != s_test_pattern_byte(i))
      return false;
  }

  return true;
}

static iohdlc_frame_t *s_take_completed_test_frame_locked(iohdlc_station_peer_t *peer) {
  iohdlc_frame_t *fp;

  if (peer->um_cmd != 0 || (peer->um_state & IOHDLC_UM_SENT) ||
      peer->um_api_frame == NULL)
    return NULL;

  fp = peer->um_api_frame;
  peer->um_api_frame = NULL;
  return fp;
}

static bool s_peer_rx_has_data(const iohdlc_station_peer_t *peer) {
  return peer->partial_read_frame != NULL ||
         !ioHdlc_frameq_isempty(&peer->i_recept_q);
}

static bool s_peer_raw_rx_deliver(iohdlc_station_peer_t *peer, iohdlc_frame_t *fp) {
  ioHdlc_frameq_insert(&peer->i_recept_q, &fp->q);
  iohdlc_condvar_signal(&peer->rx_cv);
  return true;
}

const iohdlc_peer_rx_ops_t ioHdlcPeerRawRxOps = {
  .deliver = s_peer_raw_rx_deliver,
};

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

static bool s_log2mod_supported(const uint8_t *supported_log2mods, uint8_t log2mod) {
  size_t i;

  for (i = 0U; i < 4U; ++i) {
    if (supported_log2mods[i] == log2mod)
      return true;
  }

  return false;
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
 *          - Leaves connected-mode RX dispatch unset until link-up
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
  const ioHdlcDriverCapabilities *caps = NULL;

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
  ioHdlcsp->pf_state = 0;
  if (mode == IOHDLC_OM_NDM && (ioHdlcsp->flags & IOHDLC_FLG_PRI))
    ioHdlcsp->pf_state |= IOHDLC_F_RCVED;

  /* Driver setup */
  ioHdlcsp->driver = ioHdlcsconfp->driver;

  /* Validate driver is present - station cannot operate without driver */
  if (ioHdlcsp->driver == NULL || ioHdlcsp->driver->vmt == NULL) {
    iohdlc_errno = EINVAL;
    return -1;
  }

  caps = hdlcGetCapabilities(ioHdlcsp->driver);
  if (caps == NULL) {
    iohdlc_errno = EINVAL;
    return -1;
  }

  if (!s_log2mod_supported(caps->modulo.supported_log2mods, ioHdlcsconfp->log2mod)) {
    iohdlc_errno = ENOTSUP;
    return -1;
  }

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

  /* Station init starts only from disconnected modes (NDM/ADM). */
  ioHdlcsp->rx_fn = NULL;

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
  uint32_t frame_align = IOHDLC_FRAME_POOL_ALIGNMENT;
  iohdlc_fmempool_layout_t pool_layout = iohdlc_fmempool_layout(ioHdlcsconfp->frame_arena,
                                                                ioHdlcsconfp->frame_arena_size,
                                                                frame_size, frame_align);
  
  /* Calculate the low watermark percentage. */
  uint32_t num_frames = pool_layout.count;
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
          frame_align);
  
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
  iohdlc_station_peer_t *p;

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

  if (ioHdlcsp->peers.next != NULL && ioHdlcsp->peers.prev != NULL) {
    for (p = ioHdlcsp->peers.next;
         p != (iohdlc_station_peer_t *)&ioHdlcsp->peers;
         p = p->next) {
      iohdlc_vt_deinit(&p->reply_tmr);
      iohdlc_vt_deinit(&p->t3_tmr);
    }
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
  peer->rx_ops = &ioHdlcPeerRawRxOps;
  
  /* Initialize queues */
  ioHdlc_frameq_init(&peer->i_recept_q);
  ioHdlc_frameq_init(&peer->i_retrans_q);
  ioHdlc_frameq_init(&peer->i_trans_q);
  
  /* Initialize flow control condition variables and mutex */
  iohdlc_condvar_init(&peer->tx_cv);            /* TX flow control (used with state_mutex) */
  iohdlc_condvar_init(&peer->rx_cv);            /* RX stream predicate (used with state_mutex) */
  iohdlc_bsem_init(&peer->write_gate, false);   /* Available. */
  iohdlc_bsem_init(&peer->read_gate, false);    /* Available. */
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
  if (peer == NULL) {
    iohdlc_errno = EINVAL;
    return -1;
  }

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
 * @brief   Run a HDLC TEST command/response exchange with a peer.
 * @details Queues a TEST command carrying a deterministic information field
 *          of @p len octets and waits for a TEST response echoing it exactly.
 *          The exchange does not change link mode or sequence variables.
 *
 * @param[in] peer        Peer descriptor.
 * @param[in] len         TEST information field length.
 * @param[in] timeout_ms  Maximum time to wait for completion.
 * @return  0 on echo success, -1 on error with @p iohdlc_errno set.
 * @par Errors
 * @p iohdlc_errno is set to @c EINVAL, @c ENOTSUP, @c EMSGSIZE, @c ENOMEM,
 * @c EBUSY, @c ETIMEDOUT, or @c EIO according to the rejected argument,
 * station role, resource state, timeout, or response validation failure.
 * @note    Completion signaling is private to this synchronous API; callers
 *          observe the result through the return value and @p iohdlc_errno.
 */
int32_t ioHdlcPeerTest(iohdlc_station_peer_t *peer, size_t len,
                       uint32_t timeout_ms) {
  iohdlc_station_t *s;
  iohdlc_event_listener_t listener;
  iohdlc_frame_t *cmd_fp = NULL;
  iohdlc_frame_t *rsp_fp = NULL;
  uint32_t deadline_ms;
  int32_t error = 0;
  bool signal_tx = false;
  const size_t hdr_len = 2U;

  if (peer == NULL || timeout_ms == 0U) {
    iohdlc_errno = EINVAL;
    return -1;
  }

  s = peer->stationp;
  if (s == NULL) {
    iohdlc_errno = EINVAL;
    return -1;
  }

  if (!s_test_initiator_allowed(s)) {
    iohdlc_errno = ENOTSUP;
    return -1;
  }

  if (len > peer->mifls ||
      len > ((size_t)UINT16_MAX - (s->framing.frame_offset + hdr_len))) {
    iohdlc_errno = EMSGSIZE;
    return -1;
  }

  cmd_fp = hdlcTakeFrame(&s->frame_pool);
  if (cmd_fp == NULL) {
    iohdlc_errno = ENOMEM;
    return -1;
  }

  ioHdlcBuildUFrame(s, peer, cmd_fp, IOHDLC_U_TEST, true, true);
  s_fill_test_pattern(&cmd_fp->frame[s->framing.frame_offset + hdr_len], len);
  cmd_fp->elen = (uint16_t)(cmd_fp->elen + len);

  deadline_ms = (timeout_ms == IOHDLC_WAIT_FOREVER) ?
                IOHDLC_WAIT_FOREVER : (iohdlc_time_now_ms() + timeout_ms);

  iohdlc_evt_register(&s->app_es, &listener, IOHDLC_APP_EVT_MASK_DEFAULT,
                      IOHDLC_APP_INTERNAL_TEST_DONE);

  iohdlc_mutex_lock(&peer->state_mutex);

  if (peer->um_cmd != 0 || peer->um_state != 0 ||
      peer->um_api_frame != NULL || peer->um_rsp_frame != NULL ||
      peer->frmr_condition ||
      ((IOHDLC_IS_NRM(s) || IOHDLC_IS_ABM(s)) && IOHDLC_P_SENT(s)) ||
      (IOHDLC_IS_ABM(s) && IOHDLC_P_ISRCVED(s))) {
    iohdlc_mutex_unlock(&peer->state_mutex);
    iohdlc_evt_unregister(&s->app_es, &listener);
    hdlcReleaseFrame(&s->frame_pool, cmd_fp);
    iohdlc_errno = EBUSY;
    return -1;
  }

  peer->um_cmd = IOHDLC_U_TEST;
  peer->um_api_frame = cmd_fp;
  cmd_fp = NULL;

  iohdlc_mutex_unlock(&peer->state_mutex);
  ioHdlcBroadcastFlags(s, IOHDLC_EVT_LINK_REQ);

  while (rsp_fp == NULL) {
    uint32_t remaining_ms;
    eventmask_t evt;
    eventflags_t flags;

    if (deadline_ms == IOHDLC_WAIT_FOREVER) {
      remaining_ms = IOHDLC_WAIT_FOREVER;
    } else {
      uint32_t now_ms = iohdlc_time_now_ms();
      remaining_ms = (now_ms < deadline_ms) ? (deadline_ms - now_ms) : 0U;
    }

    if (remaining_ms == 0U)
      break;

    evt = iohdlc_evt_wait_any_timeout(IOHDLC_APP_EVT_MASK_DEFAULT,
                                      remaining_ms);
    if (evt == 0)
      break;

    flags = iohdlc_evt_get_and_clear_flags(&listener);
    if (!(flags & IOHDLC_APP_INTERNAL_TEST_DONE))
      continue;

    iohdlc_mutex_lock(&peer->state_mutex);
    rsp_fp = s_take_completed_test_frame_locked(peer);
    iohdlc_mutex_unlock(&peer->state_mutex);
  }

  if (rsp_fp == NULL) {
    iohdlc_mutex_lock(&peer->state_mutex);
    rsp_fp = s_take_completed_test_frame_locked(peer);
    if (rsp_fp == NULL && peer->um_cmd == IOHDLC_U_TEST) {
      if (peer->um_api_frame != NULL) {
        cmd_fp = peer->um_api_frame;
        peer->um_api_frame = NULL;
      }
      peer->um_cmd = 0;
      peer->um_state &= (uint8_t)~IOHDLC_UM_SENT;
      ioHdlcStopReplyTimer(peer, IOHDLC_TIMER_REPLY);
      if (IOHDLC_IS_PRI(s) || IOHDLC_IS_ABM(s) || IOHDLC_IS_ADM(s))
        s->pf_state |= IOHDLC_F_RCVED;
      if (IOHDLC_IS_NRM(s) && IOHDLC_IS_PRI(s) && !IOHDLC_PEER_DISC(peer))
        ioHdlcStartReplyTimer(peer, IOHDLC_TIMER_T3,
                              s->reply_timeout_ms *
                              IOHDLC_DFL_T3_IDLE_T1_RATIO);
      signal_tx = true;
    }
    if (rsp_fp == NULL)
      error = ETIMEDOUT;
    iohdlc_mutex_unlock(&peer->state_mutex);
  }

  iohdlc_evt_unregister(&s->app_es, &listener);

  if (cmd_fp != NULL)
    hdlcReleaseFrame(&s->frame_pool, cmd_fp);

  if (signal_tx)
    ioHdlcBroadcastFlags(s, IOHDLC_EVT_TX_IFRM_ENQ);

  if (rsp_fp != NULL) {
    const size_t frame_hdr_len = s->framing.frame_offset + hdr_len;
    if (rsp_fp->elen < frame_hdr_len) {
      error = EIO;
    } else {
      const uint8_t rsp_fun = IOHDLC_FRAME_CTRL(s, rsp_fp, 0) &
                              IOHDLC_U_FUN_MASK;
      const size_t info_len = rsp_fp->elen - frame_hdr_len;
      if (rsp_fun != IOHDLC_U_TEST || info_len != len ||
          !s_test_pattern_matches(&rsp_fp->frame[frame_hdr_len], info_len))
        error = EIO;
    }
    hdlcReleaseFrame(&s->frame_pool, rsp_fp);
  }

  if (error != 0) {
    iohdlc_errno = error;
    return -1;
  }

  return 0;
}

/**
 * @brief   Submit a UI value for best-effort delivery to a connected peer.
 * @details Stores a fixed-width UI payload in the peer TX slot and wakes the
 *          TX runner. The slot is single-entry and last-value-wins.
 * @param[in] peer   Connected peer descriptor.
 * @param[in] value  UI payload to transmit.
 * @return  0 on success, -1 if the peer is invalid or disconnected.
 * @par Errors
 * @p iohdlc_errno is set to @c EINVAL for a null peer or @c ENOTCONN when
 * the peer is disconnected.
 */
int32_t ioHdlcPeerUiSend(iohdlc_station_peer_t *peer, uint32_t value) {
  iohdlc_station_t *s;

  if (peer == NULL) {
    iohdlc_errno = EINVAL;
    return -1;
  }

  s = peer->stationp;
  iohdlc_mutex_lock(&peer->state_mutex);
  if (IOHDLC_PEER_DISC(peer)) {
    iohdlc_mutex_unlock(&peer->state_mutex);
    iohdlc_errno = ENOTCONN;
    return -1;
  }

  peer->ui_tx_value = value;
  peer->ui_tx_pending = true;
  iohdlc_mutex_unlock(&peer->state_mutex);

  ioHdlcBroadcastFlags(s, IOHDLC_EVT_UI_ENQ);
  return 0;
}

/**
 * @brief   Consume the last UI value received from a peer.
 * @details Returns the cached UI payload only if a new value is pending.
 *          The cache retains the value, while the pending state is consumed.
 * @param[in] peer    Peer descriptor.
 * @param[out] value  Storage for the received UI payload.
 * @return  true if a new UI value was available, false otherwise.
 * @note    A false return means either no value was pending, or invalid
 *          arguments when @p iohdlc_errno is set to @c EINVAL.
 */
bool ioHdlcPeerUiGet(iohdlc_station_peer_t *peer, uint32_t *value) {
  if (peer == NULL || value == NULL) {
    iohdlc_errno = EINVAL;
    return false;
  }

  iohdlc_mutex_lock(&peer->state_mutex);
  if (!peer->ui_rx_pending) {
    iohdlc_mutex_unlock(&peer->state_mutex);
    return false;
  }

  *value = peer->ui_rx_value;
  peer->ui_rx_pending = false;
  iohdlc_mutex_unlock(&peer->state_mutex);
  return true;
}

/**
 * @brief   Return an application-visible snapshot of a peer's link state.
 * @param[in] peer  Peer descriptor.
 * @return  Current peer state, or @ref IOHDLC_PEER_STATE_INVALID if @p peer
 *          is null.
 * @note    The returned state is captured under the peer state mutex.
 */
iohdlc_peer_state_t ioHdlcPeerGetState(iohdlc_station_peer_t *peer) {
  iohdlc_peer_state_t state;

  if (peer == NULL) {
    iohdlc_errno = EINVAL;
    return IOHDLC_PEER_STATE_INVALID;
  }

  iohdlc_mutex_lock(&peer->state_mutex);
  if (peer->ss_state & IOHDLC_SS_ST_CONN)
    state = IOHDLC_PEER_STATE_CONNECTED;
  else if (peer->ss_state & IOHDLC_SS_TERM_ORDERLY)
    state = IOHDLC_PEER_STATE_ORDERLY_CLOSED;
  else if (peer->ss_state & IOHDLC_SS_TERM_ABORTED)
    state = IOHDLC_PEER_STATE_ABORTED;
  else
    state = IOHDLC_PEER_STATE_DISCONNECTED;
  iohdlc_mutex_unlock(&peer->state_mutex);

  return state;
}

/**
 * @brief   Register an application listener on a station event source.
 * @details The listener is bound to the calling thread. The event mask must
 *          contain one bit reserved for this listener in that thread. The
 *          caller owns @p listener and must keep it valid until unregistering
 *          it from the same thread. Event flags are station-wide and
 *          coalesced; they do not identify a peer or form a transition log.
 *
 *          Do not use @ref IOHDLC_APP_EVT_MASK_DEFAULT for a listener on a
 *          thread that also invokes synchronous ioHdlc APIs, because that bit
 *          is reserved for their internal waits.
 * @param[in] station       Station whose application events are observed.
 * @param[out] listener     Caller-owned listener object.
 * @param[in] event_mask    Single thread event bit used for notification.
 * @param[in] wanted_flags  Application event flags to receive.
 * @return  0 on success, -1 on invalid arguments.
 * @par Errors
 * @p iohdlc_errno is set to @c EINVAL when a pointer is null, the mask is not
 * a single bit, or no application flags were requested.
 */
int32_t ioHdlcAppListenerRegister(iohdlc_station_t *station,
                                  iohdlc_app_listener_t *listener,
                                  eventmask_t event_mask,
                                  eventflags_t wanted_flags) {
  if (station == NULL || listener == NULL || event_mask == 0U ||
      (event_mask & (event_mask - 1U)) != 0U || wanted_flags == 0U) {
    iohdlc_errno = EINVAL;
    return -1;
  }

  memset(listener, 0, sizeof *listener);
  listener->stationp = station;
  listener->event_mask = event_mask;
  iohdlc_evt_register(&station->app_es, &listener->listener, event_mask,
                      wanted_flags);
  return 0;
}

/**
 * @brief   Wait for and consume pending application event flags.
 * @details Pending listener flags are consumed before blocking, so a wakeup
 *          consumed by another wait in the same thread does not hide an
 *          already published notification. The caller should inspect peer
 *          state or consume the associated UI value after receiving a flag.
 * @param[in] listener    Registered application listener.
 * @param[in] timeout_ms  Maximum wait time, zero for polling, or
 *                        @ref IOHDLC_WAIT_FOREVER.
 * @return  Coalesced application flags selected by the listener, or zero on
 *          timeout.
 */
eventflags_t ioHdlcAppListenerWait(iohdlc_app_listener_t *listener,
                                   uint32_t timeout_ms) {
  eventflags_t flags;

  IOHDLC_ASSERT(listener != NULL,
                "ioHdlcAppListenerWait: null listener");
  IOHDLC_ASSERT(listener->stationp != NULL,
                "ioHdlcAppListenerWait: listener not registered");

  flags = iohdlc_evt_get_and_clear_flags(&listener->listener);
  if (flags != 0U) {
    (void)iohdlc_evt_wait_any_timeout(listener->event_mask, 0U);
    return flags;
  }

  if (iohdlc_evt_wait_any_timeout(listener->event_mask, timeout_ms) == 0U)
    return 0U;

  return iohdlc_evt_get_and_clear_flags(&listener->listener);
}

/**
 * @brief   Unregister an application event listener.
 * @details Removes the listener, clears its thread event bit, and releases
 *          the caller-owned object for reuse.
 * @param[in,out] listener  Registered listener object.
 */
void ioHdlcAppListenerUnregister(iohdlc_app_listener_t *listener) {
  eventmask_t event_mask;

  IOHDLC_ASSERT(listener != NULL,
                "ioHdlcAppListenerUnregister: null listener");
  IOHDLC_ASSERT(listener->stationp != NULL,
                "ioHdlcAppListenerUnregister: listener not registered");

  event_mask = listener->event_mask;
  iohdlc_evt_unregister(&listener->stationp->app_es, &listener->listener);
  (void)iohdlc_evt_wait_any_timeout(event_mask, 0U);
  listener->stationp = NULL;
  listener->event_mask = 0U;
}

/**
 * @brief   Establish data link connection with a peer (extended version).
 * @details Initiates connection by sending the appropriate U-frame command
 *          (SNRM/SARM/SABM) once, then waits for the protocol core to conclude
 *          the transaction or for the API safety timeout.
 *          
 *          Primary station behavior:
 *          - Sends set-mode command with P=1
 *          - Waits for UA response with F=1
 *          - Waits once for completion with a safety timeout
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
 *                      - ETIMEDOUT: No terminal link event before timeout
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
  
  iohdlc_mutex_lock(&p->state_mutex);

  /* Set unnumbered command in peer descriptor */
  p->um_cmd = u_cmd;

  /* Clear any terminal state while a new connection attempt is in flight. */
  p->ss_state &= (uint8_t)~(IOHDLC_SS_TERM_ORDERLY | IOHDLC_SS_TERM_ABORTED);

  iohdlc_mutex_unlock(&p->state_mutex);

  /* Signal TX task to send the command */
  ioHdlcBroadcastFlags(s, IOHDLC_EVT_LINK_REQ);

  /* Wait for app event with timeout.
     Multiple threads may call LinkUp on different peers concurrently.
     Verify that the event is for our peer before accepting it;
     if not, re-wait with the same timeout. */
  uint32_t timeout_ms = s_link_api_timeout_ms(s, p);
  for (;;) {
    eventmask_t evt = iohdlc_evt_wait_any_timeout(evt_mask, timeout_ms);
    if (evt == 0)
      break;

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

  /* Safety timeout expired before the protocol concluded the transaction. */
  iohdlc_evt_unregister(&s->app_es, &listener);
  s->mode = s_mode;
  iohdlc_errno = ETIMEDOUT;
  return -1;
}

/**
 * @brief   Terminate data link connection with a peer (extended version).
 * @details Sends DISC once and waits for the protocol core to conclude the
 *          disconnect transaction or for the API safety timeout.
 *          
 *          Primary station behavior:
 *          - Sends DISC with P=1
 *          - Waits for UA or DM response with F=1
 *          - Waits once for completion with a safety timeout
 *          - Closes the peer orderly on success, preserving buffered RX data
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
 *                      - ETIMEDOUT: No terminal link event before timeout
 * 
 * @note iohdlc_errno field contains detailed error code on failure.
 * @note This function blocks until disconnection completes or fails.
 * @note On successful DISC/UA or DISC/DM, TX-side state is cleared while
 *       buffered RX data remains readable.
 * @note Uses app_es event source to avoid conflicts with core events.
 * 
 */
int32_t ioHdlcStationLinkDownEx(iohdlc_station_t *s, uint32_t peer_addr,
                                eventmask_t evt_mask) {
  iohdlc_station_peer_t *p;
  iohdlc_event_listener_t listener;

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

  iohdlc_mutex_lock(&p->state_mutex);

  /* Set DISC command in peer descriptor */
  p->um_cmd = IOHDLC_U_DISC;

  /* Clear stale terminal state before starting the close transaction. */
  p->ss_state &= (uint8_t)~(IOHDLC_SS_TERM_ORDERLY | IOHDLC_SS_TERM_ABORTED);

  iohdlc_mutex_unlock(&p->state_mutex);

  /* Signal TX task to send DISC */
  ioHdlcBroadcastFlags(s, IOHDLC_EVT_LINK_REQ);

  /* Wait for app event with timeout.
     Verify that the event is for our peer before accepting it. */
  uint32_t timeout_ms = s_link_api_timeout_ms(s, p);
  for (;;) {
    eventmask_t evt = iohdlc_evt_wait_any_timeout(evt_mask, timeout_ms);
    if (evt == 0)
      break;

    eventflags_t flags = iohdlc_evt_get_and_clear_flags(&listener);

    if ((flags & IOHDLC_APP_LINK_DOWN) &&
        !(p->ss_state & IOHDLC_SS_ST_CONN)) {
      /* Our peer is disconnected. */
      iohdlc_evt_unregister(&s->app_es, &listener);
      return 0;
    }
    /* Event was for another peer: re-wait. */
  }

  /* Safety timeout expired before the protocol concluded the transaction. */
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
 * @brief   Write a logical byte sequence described by multiple vectors.
 * @details Serializes the complete operation against other writers on the
 *          peer, greedily fills I-frames across vector boundaries, and applies
 *          one timeout budget to admission and flow-control waits.
 *
 * @param[in] peer        Peer descriptor.
 * @param[in] iov         Constant input vector array.
 * @param[in] iovcnt      Number of vector entries.
 * @param[in] timeout_ms  Total timeout in milliseconds.
 *
 * @return                Bytes queued, or -1 on error.
 *
 * @note Zero-length entries are ignored and may have a null base. The
 *       aggregate length must fit in ssize_t.
 * @note Vector boundaries do not constrain I-frame boundaries.
 */
ssize_t ioHdlcWriteVTmo(iohdlc_station_peer_t *peer,
                        const iohdlc_const_iovec_t *iov, size_t iovcnt,
                        uint32_t timeout_ms) {
  iohdlc_api_timeout_t timeout;
  iohdlc_station_t *s;
  iohdlc_frame_t *fp;
  uint8_t *info_ptr;
  size_t total = 0U;
  size_t remaining;
  size_t iov_index = 0U;
  size_t iov_offset = 0U;
  size_t chunk_size;
  size_t frame_copied;
  size_t i;
  ssize_t result;
  bool signal_tx = false;

  if (peer == NULL || (iov == NULL && iovcnt != 0U)) {
    iohdlc_errno = EINVAL;
    return -1;
  }

  for (i = 0U; i < iovcnt; ++i) {
    if (iov[i].iov_base == NULL && iov[i].iov_len != 0U) {
      iohdlc_errno = EINVAL;
      return -1;
    }
    if (iov[i].iov_len > IOHDLC_SSIZE_MAX - total) {
      iohdlc_errno = EINVAL;
      return -1;
    }
    total += iov[i].iov_len;
  }

  if (total == 0U)
    return 0;

  if (IOHDLC_PEER_DISC(peer)) {
    iohdlc_errno = ENOTCONN;
    return -1;
  }

  timeout.timeout_ms = timeout_ms;
  timeout.start_ms = timeout_ms == IOHDLC_WAIT_FOREVER ?
                     0U : iohdlc_time_now_ms();
  if (iohdlc_bsem_wait_timeout(&peer->write_gate,
                               s_api_timeout_remaining(&timeout)) != MSG_OK) {
    iohdlc_errno = ETIMEDOUT;
    return -1;
  }

  s = peer->stationp;
  remaining = total;
  result = 0;

  while (remaining > 0U) {
    iohdlc_mutex_lock(&peer->state_mutex);

    while (!IOHDLC_PEER_DISC(peer) && W_WAIT_COND(s, peer)) {
      msg_t wait_result;

      if (signal_tx) {
        ioHdlcBroadcastFlags(s, IOHDLC_EVT_TX_IFRM_ENQ);
        signal_tx = false;
      }
      wait_result = iohdlc_condvar_wait_timeout(
          &peer->tx_cv, &peer->state_mutex,
          s_api_timeout_remaining(&timeout));
      if (wait_result == MSG_TIMEOUT)
        iohdlc_mutex_lock(&peer->state_mutex);
      if (wait_result == MSG_TIMEOUT && W_WAIT_COND(s, peer)) {
        iohdlc_errno = ETIMEDOUT;
        result = total != remaining ? (ssize_t)(total - remaining) : -1;
        iohdlc_mutex_unlock(&peer->state_mutex);
        goto write_done;
      }
    }

    if (IOHDLC_PEER_DISC(peer)) {
      result = (ssize_t)(total - remaining);
      iohdlc_mutex_unlock(&peer->state_mutex);
      goto write_done;
    }

    chunk_size = remaining < peer->mifls ? remaining : peer->mifls;

    /* FFF TYPE0 cannot encode a frame length equal to the flag octet. */
    if (s->framing.frame_offset == 1U && chunk_size > 1U) {
      uint32_t frame_total = s->framing.frame_offset + 1U +
                             s->framing.ctrl_size + chunk_size + s->fcs_size;
      if ((frame_total & 0xFFU) == 0x7EU)
        chunk_size /= 2U;
    }

    fp = hdlcTakeFrame(&s->frame_pool);
    iohdlc_mutex_unlock(&peer->state_mutex);
    if (fp == NULL)
      continue;

    IOHDLC_FRAME_ADDR(s, fp) = IOHDLC_IS_PRI(s) ? peer->addr : s->addr;
    IOHDLC_FRAME_CTRL(s, fp, 0) = IOHDLC_I_ID;
    info_ptr = IOHDLC_FRAME_INFO(s, fp);

    frame_copied = 0U;
    while (frame_copied < chunk_size) {
      size_t bytes_to_copy;
      const uint8_t *src;

      while (iov_index < iovcnt &&
             iov_offset == iov[iov_index].iov_len) {
        iov_index++;
        iov_offset = 0U;
      }

      bytes_to_copy = iov[iov_index].iov_len - iov_offset;
      if (bytes_to_copy > chunk_size - frame_copied)
        bytes_to_copy = chunk_size - frame_copied;
      src = (const uint8_t *)iov[iov_index].iov_base + iov_offset;
      memcpy(info_ptr + frame_copied, src, bytes_to_copy);
      frame_copied += bytes_to_copy;
      iov_offset += bytes_to_copy;
    }

    fp->elen = (uint16_t)(info_ptr + chunk_size - fp->frame);

    iohdlc_mutex_lock(&peer->state_mutex);
    ioHdlc_frameq_insert(&peer->i_trans_q, &fp->q);
    peer->i_pending_count++;
    iohdlc_mutex_unlock(&peer->state_mutex);

    signal_tx = true;
    remaining -= chunk_size;
  }

  result = (ssize_t)total;

write_done:
  if (signal_tx)
    ioHdlcBroadcastFlags(s, IOHDLC_EVT_TX_IFRM_ENQ);
  iohdlc_bsem_signal(&peer->write_gate);
  return result;
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
 * @retval -1            Error occurred (check iohdlc_errno)
 * 
 * @note Blocks when the writer pending limit is reached or the pool is not in
 *       its normal state.
 * @note Sets initial address and I-frame ID; N(S), N(R), P/F and ABM address
 *       selection are finalized during TX.
 * @note Automatically fragments data if count > mifls.
 * @note Concurrent writes on the same peer are serialized as complete logical
 *       operations.
 * 
 */
ssize_t ioHdlcWriteTmo(iohdlc_station_peer_t *peer, const void *buf,
                       size_t count, uint32_t timeout_ms) {
  iohdlc_const_iovec_t iov;

  if (peer == NULL || buf == NULL || count == 0U) {
    iohdlc_errno = EINVAL;
    return -1;
  }

  iov.iov_base = buf;
  iov.iov_len = count;
  return ioHdlcWriteVTmo(peer, &iov, 1U, timeout_ms);
}

/**
 * @brief   Read a logical byte sequence into multiple vectors.
 * @details Serializes the complete operation against other readers on the
 *          peer, distributes the stream consecutively across vector
 *          boundaries, and applies one timeout budget to admission and data
 *          waits.
 *
 * @param[in] peer        Peer descriptor.
 * @param[out] iov        Output vector array.
 * @param[in] iovcnt      Number of vector entries.
 * @param[in] timeout_ms  Total timeout in milliseconds.
 *
 * @return                Bytes read, 0 on orderly EOF, or -1 on error.
 *
 * @note Zero-length entries are ignored and may have a null base. The
 *       aggregate length must fit in ssize_t.
 * @note Buffered data is returned before orderly EOF or an aborted-link error.
 */
ssize_t ioHdlcReadVTmo(iohdlc_station_peer_t *peer,
                       const iohdlc_iovec_t *iov, size_t iovcnt,
                       uint32_t timeout_ms) {
  iohdlc_api_timeout_t timeout;
  iohdlc_station_t *s;
  iohdlc_frame_t *fp;
  uint8_t *info_ptr;
  size_t total = 0U;
  size_t total_read = 0U;
  size_t iov_index = 0U;
  size_t iov_offset = 0U;
  size_t info_len;
  size_t available_bytes;
  size_t i;
  ssize_t result;

  if (peer == NULL || (iov == NULL && iovcnt != 0U)) {
    iohdlc_errno = EINVAL;
    return -1;
  }

  for (i = 0U; i < iovcnt; ++i) {
    if (iov[i].iov_base == NULL && iov[i].iov_len != 0U) {
      iohdlc_errno = EINVAL;
      return -1;
    }
    if (iov[i].iov_len > IOHDLC_SSIZE_MAX - total) {
      iohdlc_errno = EINVAL;
      return -1;
    }
    total += iov[i].iov_len;
  }

  if (total == 0U)
    return 0;

  timeout.timeout_ms = timeout_ms;
  timeout.start_ms = timeout_ms == IOHDLC_WAIT_FOREVER ?
                     0U : iohdlc_time_now_ms();
  if (iohdlc_bsem_wait_timeout(&peer->read_gate,
                               s_api_timeout_remaining(&timeout)) != MSG_OK) {
    iohdlc_errno = ETIMEDOUT;
    return -1;
  }

  s = peer->stationp;
  result = 0;
  iohdlc_mutex_lock(&peer->state_mutex);
  peer->ss_state |= IOHDLC_SS_RECVING;

  while (total_read < total) {
    while (!s_peer_rx_has_data(peer) && !IOHDLC_PEER_DISC(peer)) {
      msg_t wait_result = iohdlc_condvar_wait_timeout(
          &peer->rx_cv, &peer->state_mutex,
          s_api_timeout_remaining(&timeout));

      if (wait_result == MSG_TIMEOUT)
        iohdlc_mutex_lock(&peer->state_mutex);
      if (wait_result == MSG_TIMEOUT &&
          !s_peer_rx_has_data(peer) && !IOHDLC_PEER_DISC(peer)) {
        if (total_read != 0U)
          goto read_done;
        iohdlc_errno = ETIMEDOUT;
        result = -1;
        goto read_done;
      }
    }

    if (!s_peer_rx_has_data(peer)) {
      if (total_read != 0U)
        goto read_done;
      if (IOHDLC_PEER_ORDERLY_CLOSED(peer))
        goto read_done;
      if (IOHDLC_PEER_ABORTED(peer)) {
        iohdlc_errno = ECONNRESET;
        result = -1;
        goto read_done;
      }
      iohdlc_errno = ENOTCONN;
      result = -1;
      goto read_done;
    }

    fp = peer->partial_read_frame;
    if (fp == NULL) {
      iohdlc_frame_q_t *qh = ioHdlc_frameq_remove(&peer->i_recept_q);
      fp = IOHDLC_FRAME_FROM_Q(qh);
      peer->partial_read_offset = 0U;
    }

    info_ptr = IOHDLC_FRAME_INFO(s, fp);
    info_len = fp->elen -
               (s->framing.frame_offset + 1U + s->framing.ctrl_size);
    available_bytes = info_len - peer->partial_read_offset;

    while (available_bytes > 0U && total_read < total) {
      size_t bytes_to_copy;
      uint8_t *dest;

      while (iov_index < iovcnt &&
             iov_offset == iov[iov_index].iov_len) {
        iov_index++;
        iov_offset = 0U;
      }

      bytes_to_copy = iov[iov_index].iov_len - iov_offset;
      if (bytes_to_copy > available_bytes)
        bytes_to_copy = available_bytes;
      dest = (uint8_t *)iov[iov_index].iov_base + iov_offset;
      memcpy(dest, info_ptr + peer->partial_read_offset, bytes_to_copy);

      iov_offset += bytes_to_copy;
      peer->partial_read_offset += bytes_to_copy;
      total_read += bytes_to_copy;
      available_bytes -= bytes_to_copy;
    }

    if (peer->partial_read_offset < info_len) {
      peer->partial_read_frame = fp;
      goto read_done;
    }

    hdlcReleaseFrame(&s->frame_pool, fp);
    peer->partial_read_frame = NULL;
    peer->partial_read_offset = 0U;

    if (IOHDLC_IS_BUSY(s) &&
        hdlcPoolGetState(&s->frame_pool) == IOHDLC_POOL_NORMAL)
      ioHdlcBroadcastFlags(s, IOHDLC_EVT_POOL_ST_CHG);
  }

read_done:
  peer->ss_state &= ~IOHDLC_SS_RECVING;
  iohdlc_mutex_unlock(&peer->state_mutex);
  iohdlc_bsem_signal(&peer->read_gate);

  if (result == 0)
    result = (ssize_t)total_read;
  return result;
}

/**
 * @brief   Read data from peer via HDLC I-frames.
 * @details Blocks until data becomes readable, the peer reaches a terminal
 *          state, or the timeout expires. Buffered RX data is drained in-order.
 *          On orderly close, returns 0 once all buffered data has been read.
 *          On aborted links, returns an error once buffered data has been read.
 *          Supports partial frame reads by preserving a shared stream cursor.
 *
 * @param[in] peer       Peer descriptor
 * @param[out] buf       Buffer to receive data
 * @param[in] count      Maximum bytes to read
 * @param[in] timeout_ms Timeout in milliseconds (IOHDLC_WAIT_FOREVER for blocking)
 *
 * @return               Bytes read on success, 0 on orderly EOF, -1 on error
 * @retval >0            Number of bytes read
 * @retval 0             No more data: peer closed orderly and RX drained
 * @retval -1            Error occurred (check iohdlc_errno)
 *
 * @note Blocks until the stream predicate changes or timeout expires.
 * @note Releases frame back to pool when fully consumed (may trigger watermark).
 * @note Supports partial reads: call multiple times to consume large frames.
 * @note Concurrent reads on the same peer are serialized as complete logical
 *       operations.
 *
 */
ssize_t ioHdlcReadTmo(iohdlc_station_peer_t *peer, void *buf,
                      size_t count, uint32_t timeout_ms) {
  iohdlc_iovec_t iov;

  if (peer == NULL) {
    iohdlc_errno = EINVAL;
    return -1;
  }
  if (count == 0U)
    return 0;
  if (buf == NULL) {
    iohdlc_errno = EINVAL;
    return -1;
  }

  iov.iov_base = buf;
  iov.iov_len = count;
  return ioHdlcReadVTmo(peer, &iov, 1U, timeout_ms);
}

/** @} */
