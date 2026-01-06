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
 * @file    include/ioHdlcstream_driver.h
 * @brief   ioHdlcDriver wrapper using ioHdlcStream core (OS-agnostic w.r.t. HAL).
 */

#ifndef IOHDLCSTREAM_DRIVER_H
#define IOHDLCSTREAM_DRIVER_H

#include "ioHdlcdriver.h"
#include "ioHdlcframe.h"
#include "ioHdlcframepool.h"
#include "ioHdlcqueue.h"
#include "ioHdlcll.h"
#include "ioHdlcstream.h"
#include "ioHdlcosal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ioHdclStreamDriver {
  const struct _iohdlc_driver_vmt *vmt;
  _iohdlc_driver_data

  ioHdlcStream         core;

  iohdlc_sem_t         raw_recept_sem;
  iohdlc_frame_q_t     raw_recept_q;

  bool apply_transparency;
  bool has_frame_format;
} ioHdclStreamDriver;

void ioHdclStreamDriverInit(ioHdclStreamDriver *uhp);

#ifdef __cplusplus
}
#endif

#endif /* IOHDLCSTREAM_DRIVER_H */
