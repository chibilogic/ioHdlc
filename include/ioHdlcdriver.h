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
 * @file    include/ioHdlcdriver.h
 * @brief   HDLC driver interface definition header.
 * @details Defines the framed-driver contract used by the station layer. A
 *          driver implementation owns the translation between protocol frames
 *          and the underlying transport strategy, including configuration,
 *          start/stop lifecycle, and receive/transmit operations.
 *
 *          Lifecycle requirements:
 *          - configuration is validated before start;
 *          - start binds the driver to a transport/backend and a frame pool;
 *          - receive/transmit operations are valid only while the driver is
 *            started;
 *          - stop releases runtime resources owned by the driver instance.
 *
 *          Ownership requirements:
 *          - frame ownership passed to send/receive operations is defined by
 *            the implementation and must be documented by concrete drivers;
 *          - capability structures returned by the driver are immutable and
 *            owned by the driver implementation.
 *
 * @addtogroup ioHdlc_drivers
 * @{
 */

#ifndef IOHDLCDRIVER_H_
#define IOHDLCDRIVER_H_

#include "ioHdlctypes.h"
#include "ioHdlcframe.h"
#include "ioHdlcframepool.h"

/*===========================================================================*/
/* Driver Capabilities Structures                                           */
/*===========================================================================*/

/**
 * @brief Driver FCS (Frame Check Sequence) capabilities.
 */
typedef struct {
  uint8_t  supported_sizes[4];  /**< Array of supported FCS sizes (e.g., [0,2,4,0]) */
  uint8_t  default_size;        /**< Default FCS size (e.g., 2 for 16-bit CRC) */
} ioHdlcDriverFcsCapabilities;

/**
 * @brief Driver transparency capabilities.
 */
typedef struct {
  bool     hw_support;          /**< true if transparency implemented in hardware */
  bool     sw_available;        /**< true if driver can apply transparency in software */
} ioHdlcDriverTransparencyCapabilities;

/**
 * @brief Driver FFF (Frame Format Field) capabilities.
 */
typedef struct {
  uint8_t  supported_types[4];  /**< Array of supported FFF sizes: 0=none, 1=TYPE0, 2=TYPE1 */
  uint8_t  default_type;        /**< Default FFF type (0=none, 1=TYPE0, 2=TYPE1) */
  bool     hw_support;          /**< true if FFF handled in hardware */
} ioHdlcDriverFffCapabilities;

/**
 * @brief Driver modulo capabilities.
 */
typedef struct {
  uint8_t  supported_log2mods[4]; /**< Array of supported log2(modulus) values (e.g., [3,7,0,0]) */
} ioHdlcDriverModuloCapabilities;

/**
 * @brief Complete driver capabilities.
 * @note Driver must provide this via get_capabilities() before start().
 */
typedef struct {
  ioHdlcDriverModuloCapabilities       modulo;
  ioHdlcDriverFcsCapabilities          fcs;
  ioHdlcDriverTransparencyCapabilities transparency;
  ioHdlcDriverFffCapabilities          fff;
} ioHdlcDriverCapabilities;

/*===========================================================================*/
/* Driver VMT Methods                                                        */
/*===========================================================================*/

#define _iohdlc_driver_methods                                      \
  void (*start)(void *ip, void *phydrvp, void *phyconfigp,          \
      ioHdlcFramePool *fpp);                                        \
  void (*stop)(void *ip);                                            \
  int32_t (*send_frame)(void *ip, iohdlc_frame_t *fp);              \
  iohdlc_frame_t * (*recv_frame)(void *ip, iohdlc_timeout_t tmo);   \
  const ioHdlcDriverCapabilities* (*get_capabilities)(void *ip);     \
  int32_t (*configure)(void *ip, uint8_t fcs_size, bool transparency, uint8_t fff_type);

#define _iohdlc_driver_data     \
  ioHdlcFramePool *fpp;

/**
 * @brief   Virtual method table for framed driver implementations.
 */
struct _iohdlc_driver_vmt {
  _iohdlc_driver_methods
};

/**
 * @brief   Generic framed driver interface object.
 * @details Concrete implementations embed this structure as their public base
 *          and extend it with transport-specific runtime state.
 *          The @p fpp field points to the frame pool bound at start time.
 */
typedef struct {
  const struct _iohdlc_driver_vmt *vmt;
  _iohdlc_driver_data
} ioHdlcDriver;

/**
 * @brief   Hdlc driver start.
 * @details The station uses this method to start the hdlc driver instance
 *          @p ip.
 * @note    The implementation shall call this method only once, before calling
 *          any other runtime method.
 * @note    Ownership of @p phyp and @p phyconfp is implementation-defined; the
 *          concrete driver must document whether those objects are borrowed or
 *          retained after start.
 *
 * @param[in]   ip        ioHdlcDriver instance pointer
 * @param[in]   phyp      pointer to the physical driver to use.
 * @param[in]   phyconfp  pointer to the configuration of the physical driver.
 * @param[in]   fpp       pointer to the frame pool to use.
 */
#define hdlcStart(ip, phyp, phyconfp, fpp)  ((ip)->vmt->start(ip, phyp, \
                  phyconfp, fpp))                                       \

/**
 * @brief   Hdlc driver stop.
 * @details The station uses this method to stop the hdlc driver instance
 *          @p ip, releasing resources and terminating background threads.
 * @note    Can be called multiple times safely (idempotent).
 *
 * @param[in]   ip        ioHdlcDriver instance pointer
 */
#define hdlcStop(ip)  ((ip)->vmt->stop(ip))

/**
 * @brief   Hdlc send frame method.
 * @details The station uses this method to submit the frame @p fp to the link
 *          managed by the @p ip driver instance.
 * @note    Success means that the driver accepted the frame for transmission;
 *          it does not guarantee that the frame has already reached the wire.
 * @note    The concrete driver defines whether it consumes, retains, or merely
 *          borrows the frame reference while transmission is pending.
 *
 * @param[in]   ip    ioHdlcDriver instance pointer
 * @param[in]   fp    pointer to the frame to send.
 * @return            0 on success, errno-compatible error code otherwise.
 */
#define hdlcSendFrame(ip, fp)         ((ip)->vmt->send_frame(ip, fp))

/**
 * @brief   Hdlc receive frame method.
 * @details The station uses this method to receive a frame from a peer
 *          over the link managed by the @p ip driver instance. If
 *          no received frame is available it blocks for a maximum
 *          of @p tmo milliseconds.
 * @note    The implementation may use queuing mechanism in order to avoid
 *          frame overrun.
 * @note    The returned frame is owned by the caller until it is released back
 *          to the frame pool according to the selected integration model.
 * @note    Timeout semantics depend on the driver/backend pair, but a return
 *          value of NULL must always mean that no frame has been handed back to
 *          the caller.
 *
 * @param[in]   ip    ioHdlcDriver instance pointer
 * @param[in]   tmo   timeout in milliseconds
 *
 * @return            the pointer to the received frame.
 * @retval NULL       if timeout expires.
 */
#define hdlcRecvFrame(ip, tmo)        ((ip)->vmt->recv_frame(ip, tmo))

/**
 * @brief   Hdlc get driver capabilities method.
 * @details Query driver capabilities (FCS sizes, transparency, FFF support).
 *          Can be called before start() to validate configuration.
 * @note    Returns pointer to static const structure.
 * @note    The returned structure must not be modified by the caller.
 *
 * @param[in]   ip    ioHdlcDriver instance pointer
 *
 * @return            Pointer to driver capabilities structure
 */
#define hdlcGetCapabilities(ip)       ((ip)->vmt->get_capabilities(ip))

/**
 * @brief   Hdlc configure driver method.
 * @details Configure driver with FCS size, transparency, and FFF settings.
 *          Must be called after validation and before start().
 * @note    Returns errno-compatible error code.
 * @note    Valid configuration combinations are implementation-specific and
 *          must be validated by the concrete driver.
 *
 * @param[in]   ip            ioHdlcDriver instance pointer
 * @param[in]   fcs_size      FCS size in bytes (0, 2, 4, ...)
 * @param[in]   transparency  true to enable transparency encoding/decoding
 * @param[in]   fff           FFF type selector (0, 1, 2, ... depending on driver capabilities)
 *
 * @return                    0 on success, errno-compatible error code otherwise
 * @retval 0                  Success
 * @retval EINVAL             Invalid configuration (e.g., fff && transparency)
 * @retval ENOTSUP            Requested feature not supported by driver
 */
#define hdlcConfigure(ip, fcs, tr, ff) ((ip)->vmt->configure(ip, fcs, tr, ff))

#endif /* IOHDLCDRIVER_H_ */

/** @} */
