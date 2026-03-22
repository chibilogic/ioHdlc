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
 * @file    include/ioHdlclist.h
 * @brief   HDLC peer list definitions header.
 * @details Defines the minimal intrusive doubly-linked list helpers used to
 *          keep track of station peers.
 *
 *          These helpers are intentionally allocation-free and assume that the
 *          caller enforces list membership invariants and synchronization.
 *
 * @addtogroup ioHdlc_frames
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
 * @details Initializes an empty circular list head.
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
 * @note    The caller must ensure that @p ssp is not already linked in
 *          another peer list.
 */
static inline void ioHdlc_peerl_insert(iohdlc_peer_list_t *lp, iohdlc_station_peer_t *ssp) {

  ssp->next = (iohdlc_station_peer_t *)lp;
  ssp->prev = lp->prev;
  ssp->prev->next = ssp;
  lp->prev = ssp;
}

/**
 * @brief   Delete the station state @p ssp from its own list.
 * @note    This helper does not reinitialize @p ssp after removal.
 */
static inline void ioHdlc_peerl_delete(iohdlc_station_peer_t *ssp) {

  ssp->prev->next = ssp->next;
  ssp->next->prev = ssp->prev;
}

/** @} */

#endif /* IOHDLCLIST_H_ */

/** @} */
