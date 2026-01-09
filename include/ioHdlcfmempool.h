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
 * @file    include/ioHdlcfmempool.h
 * @brief   HDLC frame pool factory abstraction
 * @details
 *
 * @addtogroup hdlc_types
 * @{
 */

#ifndef IOHDLCFMEMPOOL_H_
#define IOHDLCFMEMPOOL_H_

/*
 * @extends @p ioHdlcFramePool
 */
#define _iohdlc_fmempool_methods    \
  _iohdlc_framepool_methods         \

#define _iohdlc_fmempool_data       \
  _iohdlc_framepool_data            \
  iohdlc_memory_pool_t mp;          \


/**
 * @brief   @p ioHdlcFrameMemPool vmt.
 */
struct _iohdlc_fmempool_vmt {
  _iohdlc_fmempool_methods
};

/**
 * @brief   HDLC frame mem pool class.
 */
typedef struct {
  const struct _iohdlc_fmempool_vmt *vmt;
  _iohdlc_fmempool_data
} ioHdlcFrameMemPool;

#ifdef __cplusplus
extern "C" {
#endif
  void fmpInit(ioHdlcFrameMemPool *fmpp, uint8_t *arena, size_t arenasize, size_t framesize, uint32_t framealign);
#ifdef __cplusplus
}
#endif

#endif /* IOHDLCFMEMPOOL_H_ */

/** @} */
