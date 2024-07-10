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

#include "ctype.h"
#include "ioHdlc.h"
#include "ioHdlclist.h"

/**
 * HDLC core.
 */

/*===========================================================================*/
/* Module local definitions.                                                 */
/*===========================================================================*/

/*===========================================================================*/
/* Module exported variables.                                                */
/*===========================================================================*/

/*===========================================================================*/
/* Module local variables and types.                                         */
/*===========================================================================*/

/*===========================================================================*/
/* Module local functions.                                                   */
/*===========================================================================*/

static void transmitterTask(void) {

}

static void receiverTask(void) {

}

/*===========================================================================*/
/* Module exported functions.                                                */
/*===========================================================================*/

iohdlc_station_peer_t *addr2peer(iohdlc_station_t *ioHdlcsp, uint32_t peer_addr) {
  iohdlc_station_peer_t *ssp = (iohdlc_station_peer_t *)&ioHdlcsp->peers;

  while (ssp->next != (iohdlc_station_peer_t *)&ioHdlcsp->peers) {
    ssp = ssp->next;
    if ((uint32_t)ssp->peer_addr == peer_addr)
      return ssp;
  }
  return NULL;
}

void ioHdlcStationUp(iohdlc_station_t *ioHdlcsp) {
  (void)ioHdlcsp;
}

void ioHdlcStationDown(iohdlc_station_t *ioHdlcsp) {
  (void)ioHdlcsp;
}

int32_t ioHdlcWrite(iohdlc_station_peer_t *peerp, const void *buf, size_t count) {
  iohdlc_station_t *ioHdlcsp = peerp->stationp;

  switch (ioHdlcsp->mode) {
  default:
    return -1; /* EBADF, peer not connected. */
  }
  return 0;
}

int32_t ioHdlcRead(iohdlc_station_peer_t *peerp, void *buf, size_t count) {
  iohdlc_station_t *ioHdlcsp = peerp->stationp;

  switch (ioHdlcsp->mode) {
  default:
    return -1; /* EBADF, peer not connected. */
  }
  return 0;
}

void ioHdlcStationInit(iohdlc_station_t *ioHdlcsp, uint8_t mode, uint8_t modulus,
    ioHdlcDriver *driver, ioHdlcFramePool *fpp) {
  uint32_t mod2 = 0;

  while (modulus >>= 1)
    ++mod2;
  ioHdlcsp->mode = mode;
  ioHdlcsp->modulus = mod2;
  ioHdlcsp->pfoctet = (mod2 + 1) / 8;
  ioHdlcsp->frame_pool = fpp;
  ioHdlc_frameq_init(&ioHdlcsp->ni_recept_q);
  ioHdlc_frameq_init(&ioHdlcsp->ni_trans_q);
  ioHdlc_peerl_init(&ioHdlcsp->peers);
  ioHdlcsp->driver = driver;
}
