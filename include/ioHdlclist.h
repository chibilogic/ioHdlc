/*
 * ioHdlc
 * Copyright (C) 2024 Isidoro Orabona
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * This software is dual-licensed:
 *  - GNU Lesser General Public License v3.0 (or later)
 *  - Commercial license (available from Chibilogic s.r.l.)
 *
 * For commercial licensing inquiries:
 *   info@chibilogic.com
 *
 * See the LICENSE file for details.
 */
/**
 * @file    include/ioHdlclist.h
 * @brief   HDLC peer list definitions header.
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
