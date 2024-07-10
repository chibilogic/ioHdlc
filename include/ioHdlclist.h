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
 * @file    include/ioHdlclist.h
 * @brief   HDLC perr list definitions header.
 * @details
 *
 * @addtogroup hdlc_types
 * @{
 */

#ifndef IOHDLCLIST_H_
#define IOHDLCLIST_H_

/*===========================================================================*/
/* Module inline functions.                                                  */
/*===========================================================================*/

/**
 * @name    Peer list manipulation functions
 * @{
 */

/**
 * @brief   Init the @p lp peer list.
 */
static inline void ioHdlc_peerl_init(iohdlc_peer_list_t *lp) {
  lp->next = (iohdlc_station_peer_t *)lp;
  lp->prev = (iohdlc_station_peer_t *)lp;
}

/**
 * @brief   Check if the @p lp peer list is empty.
 */
static inline bool ioHdlc_peerl_isempty(const iohdlc_peer_list_t *lp) {
  return (lp->next == (iohdlc_station_peer_t *)lp);
}

/**
 * @brief   Insert the station state @p ssp into the @p lp peer list.
 */
static inline void ioHdlc_peerl_insert(iohdlc_peer_list_t *lp, iohdlc_station_peer_t *ssp) {

  ssp->next = (iohdlc_station_peer_t *)lp;
  ssp->prev = lp->prev;
  ssp->prev->next = ssp;
  lp->prev = ssp;
}

/**
 * @brief   Delete the station state @p ssp from its own list.
 */
static inline void ioHdlc_peerl_delete(iohdlc_station_peer_t *ssp) {

  ssp->prev->next = ssp->next;
  ssp->next->prev = ssp->prev;
}

/** @} */

#endif /* IOHDLCLIST_H_ */

/** @} */
