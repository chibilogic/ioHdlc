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

#define _iohdlc_driver_methods                                      \
  size_t (*send_frame)(void *ip, iohdlc_frame_t *fp);               \
  iohdlc_frame_t * (*recv_frame)(void *ip, iohdlc_timeout_t tmo);   \
  bool (*get_hwtransparency)(void *ip);                             \
  void (*set_applytransparency)(void *ip, bool tr);                 \
  void (*set_hasframeformat)(void *ip, bool hff);                   \

#define _iohdlc_driver_data     \
  ioHdlcFramePool *fpp;         \

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
 * @brief   Hdlc get frame hardware transparency method.
 * @details Get the octet/bit transparency hardware capability of the driver.
 * @note    The driver must respond to this method even if it is not
 *          started yet.
 *
 * @param[in]   ip    ioHdlcDriver instance pointer
 *
 * @return            true, if the driver supports and applies octet/bit
 *                    transparency/FCS/Flag in hardware, else false.
 */
#define hdlcTransparency(ip)          ((ip)->vmt->get_hwtransparency(ip))

/**
 * @brief   Hdlc apply frame transparency method.
 * @details Set the software octet transparency for the driver.
 * @note
 *
 * @param[in]   ip    ioHdlcDriver instance pointer
 * @param[in]   tr    set to true to request the driver to
 *                    apply software octet transparency.
 */
#define hdlcApplyTransparency(ip, tr) ((ip)->vmt->set_applytransparency(ip, tr))

/**
 * @brief   Hdlc set has frame format field method.
 * @details If set to true, the driver shall apply and use the additional
 *          frame format field.
 * @note
 *
 * @param[in]   ip    ioHdlcDriver instance pointer
 * @param[in]   tr    true or false
 */
#define hdlcHasFrameFormat(ip, tr)    ((ip)->vmt->set_hasframeformat(ip, tr))

#endif /* IOHDLCDRIVER_H_ */

/** @} */
