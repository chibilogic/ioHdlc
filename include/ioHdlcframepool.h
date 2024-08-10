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
 * @file    include/ioHdlcframepool.h
 * @brief   HDLC frame pool definitions header.
 * @details
 *
 * @addtogroup hdlc_types
 * @{
 */

#ifndef IOHDLCFRAMEPOOL_H_
#define IOHDLCFRAMEPOOL_H_

/**
 * @brief @p ioHdlcFramePool interface methods
 */
#define _iohdlc_framepool_methods                   \
  iohdlc_frame_t * (*take)(void *ip);               \
  void (*release)(void *ip, iohdlc_frame_t *fp);    \
  void (*addref)(iohdlc_frame_t *fp);               \

/*
 * @brief   @p ioHdlcFramePool specific data.
 * @note    pure interface.
 */
#define _iohdlc_framepool_data                      \
  size_t framesize;                                 \

/**
 * @brief   @p ioHdlcFramePool vmt.
 */
struct _iohdlc_framepool_vmt {
  _iohdlc_framepool_methods
};

/**
 * @brief   HDLC frame pool base class.
 */
typedef struct {
  const struct _iohdlc_framepool_vmt *vmt;
  _iohdlc_framepool_data
} ioHdlcFramePool;

/**
 * @brief   Hdlc get a new frame from a frame pool.
 * @details Use this define to request a free frame buffer
 *          from the frame pool pointed by @p fpp.
 *
 * @param[in]   fpp   ioHdlcFramePool instance pointer
 *
 * @return            the pointer to the allocated frame.
 * @retval NULL       if a free frame is not available.
 */
#define hdlcTakeFrame(fpp)          ((fpp)->vmt->take(fpp))

/**
 * @brief   Hdlc release a frame to a frame pool.
 * @details Use this define to release the @p fp frame buffer.
 *          It will be returned to the frame pool pointed by @p fpp,
 *          if reference count reach 0.
 *
 * @param[in]   fpp   ioHdlcFramePool instance pointer
 * @param[in]   fp    the pointer to the frame that is being released.
 */
#define hdlcReleaseFrame(fpp, fp)   ((fpp)->vmt->release(fpp, fp))

/**
 * @brief   Hdlc add a reference to a frame.
 * @details Use this define to share the frame between
 *          different threads.
 *
 * @param[in]   fpp   ioHdlcFramePool instance pointer
 * @param[in]   fp    the pointer to the frame referenced.
 *
 */
#define hdlcAddRef(fpp, fp)         ((fpp)->vmt->addref(fp))

#endif /* IOHDLCFRAMEPOOL_H_ */

/** @} */
