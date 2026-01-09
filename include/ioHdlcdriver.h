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
 * @file    include/ioHdlcdriver.h
 * @brief   HDLC driver interface definition header.
 * @details
 *
 * @addtogroup hdlc_drivers
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
 * @brief Driver FCS (Frame Check Sequence) capabilities
 */
typedef struct {
  uint8_t  supported_sizes[4];  /**< Array of supported FCS sizes (e.g., [0,2,4,0]) */
  uint8_t  default_size;        /**< Default FCS size (e.g., 2 for 16-bit CRC) */
  bool     hw_support;          /**< true if FCS computed/verified in hardware */
} ioHdlcDriverFcsCapabilities;

/**
 * @brief Driver transparency capabilities
 */
typedef struct {
  bool     hw_support;          /**< true if transparency implemented in hardware */
  bool     sw_available;        /**< true if driver can apply transparency in software */
} ioHdlcDriverTransparencyCapabilities;

/**
 * @brief Driver FFF (Frame Format Field) capabilities
 */
typedef struct {
  uint8_t  supported_types[4];  /**< Array of supported FFF sizes: 0=none, 1=TYPE0, 2=TYPE1 */
  uint8_t  default_type;        /**< Default FFF type (0=none, 1=TYPE0, 2=TYPE1) */
  bool     hw_support;          /**< true if FFF handled in hardware */
} ioHdlcDriverFffCapabilities;

/**
 * @brief Complete driver capabilities
 * @note Driver must provide this via get_capabilities() before start()
 */
typedef struct {
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
  size_t (*send_frame)(void *ip, iohdlc_frame_t *fp);               \
  iohdlc_frame_t * (*recv_frame)(void *ip, iohdlc_timeout_t tmo);   \
  const ioHdlcDriverCapabilities* (*get_capabilities)(void *ip);     \
  int32_t (*configure)(void *ip, uint8_t fcs_size, bool transparency, uint8_t fff_type);

#define _iohdlc_driver_data     \
  ioHdlcFramePool *fpp;

/**
 * HDLC driver interface
 */
struct _iohdlc_driver_vmt {
  _iohdlc_driver_methods
};

typedef struct {
  const struct _iohdlc_driver_vmt *vmt;
  _iohdlc_driver_data
} ioHdlcDriver;

/**
 * @brief   Hdlc driver start.
 * @details The station uses this method to start the hdlc driver instance
 *          @p ip
 * @note    The implementation shall call this method only once, before calling
 *          any other method.
 *
 * @param[in]   ip        ioHdlcDriver instance pointer
 * @param[in]   phyp      pointer to the physical driver to use.
 * @param[in]   phyconfp  pointer to the configuration of the physical driver.
 * @param[in]   fpp       pointer to the frame pool to use.
 */
#define hdlcStart(ip, phyp, phyconfp, fpp)  ((ip)->vmt->start(ip, phyp, \
                  phyconfp, fpp))                                       \

/**
 * @brief   Hdlc send frame method.
 * @details The station uses this method to send the frame @p fp over the link
 *          managed by the @p ip driver instance. It blocks if the driver is
 *          busy.
 * @note    The implementation shall not use queuing mechanism. The station
 *          expects that the transmission of the frame to be in progress when
 *          returning from the call.
 *
 * @param[in]   ip    ioHdlcDriver instance pointer
 * @param[in]   fp    pointer to the frame to send.
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
 *
 * @param[in]   ip            ioHdlcDriver instance pointer
 * @param[in]   fcs_size      FCS size in bytes (0, 2, 4, ...)
 * @param[in]   transparency  true to enable transparency encoding/decoding
 * @param[in]   fff           true to enable Frame Format Field
 *
 * @return                    0 on success, errno-compatible error code otherwise
 * @retval 0                  Success
 * @retval EINVAL             Invalid configuration (e.g., fff && transparency)
 * @retval ENOTSUP            Requested feature not supported by driver
 */
#define hdlcConfigure(ip, fcs, tr, ff) ((ip)->vmt->configure(ip, fcs, tr, ff))

#endif /* IOHDLCDRIVER_H_ */

/** @} */
