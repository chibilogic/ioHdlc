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
 * @file    include/ioHdlcqueue.h
 * @brief   HDLC queue definitions header.
 * @details
 *
 * @addtogroup hdlc_types
 * @{
 */

#ifndef IOHDLCQUEUE_H_
#define IOHDLCQUEUE_H_

/*===========================================================================*/
/* Module inline functions.                                                  */
/*===========================================================================*/

/**
 * @name    Frame queue manipulation functions
 * @{
 */

/**
 * @brief   Init the @p qp frame queue.
 */
static inline void ioHdlc_frameq_init(iohdlc_frame_q_t *qp) {
  qp->next = (iohdlc_frame_t *)qp;
  qp->prev = (iohdlc_frame_t *)qp;
}

/**
 * @brief   Check if the @p qp queue is empty.
 */
static inline bool ioHdlc_frameq_isempty(const iohdlc_frame_q_t *qp) {
  return (qp->next == (iohdlc_frame_t *)qp);
}

/**
 * @brief   Insert the frame @p fp into the @p qp frame queue.
 */
static inline void ioHdlc_frameq_insert(iohdlc_frame_q_t *qp, iohdlc_frame_t *fp) {

  fp->next = (iohdlc_frame_t *)qp;
  fp->prev = qp->prev;
  fp->prev->next = fp;
  qp->prev = fp;
}

/**
 * @brief   Remove a frame from the @p qp queue in natural fifo order.
 */
static inline iohdlc_frame_t *ioHdlc_frameq_remove(iohdlc_frame_q_t *qp) {

  iohdlc_frame_t *rfp = qp->next;
  qp->next = rfp->next;
  qp->next->prev = (iohdlc_frame_t *)qp;

  return rfp;
}

/**
 * @brief   Remove a frame from the @p qp queue in natural fifo order and
 *          lookahead the next frame in @p *next_fp without removing it.
 */
static inline iohdlc_frame_t *ioHdlc_frameq_remove_la(
  iohdlc_frame_q_t *qp,
  iohdlc_frame_t  **next_fp) {

  iohdlc_frame_t *rfp = qp->next;
  qp->next = rfp->next;
  qp->next->prev = (iohdlc_frame_t *)qp;
  if (next_fp != NULL) {
    if (ioHdlc_frameq_isempty(qp))
      *next_fp = NULL;
    else
      *next_fp = qp->next;
  }
  return rfp;
}

/**
 * @brief   Remove a frame from the @p qp queue in lifo order.
 */
static inline iohdlc_frame_t *ioHdlc_frameq_lifo_remove(iohdlc_frame_q_t *qp) {

  iohdlc_frame_t *rfp = qp->prev;
  qp->prev = rfp->prev;
  qp->prev->next = (iohdlc_frame_t *)qp;

  return rfp;
}

/**
 * @brief   Delete the frame @p fp from its own queue.
 */
static inline void ioHdlc_frameq_delete(iohdlc_frame_t *fp) {

  fp->prev->next = fp->next;
  fp->next->prev = fp->prev;
}

/**
 * @brief   Move a set of consecutive frames [@p source_from_fp, @p source_to_fp]
 *          from their own queue to the head of a destination @p dest_qp queue
 */
static inline void ioHdlc_frameq_move(iohdlc_frame_q_t *dest_qp,
    iohdlc_frame_t *source_from_fp, iohdlc_frame_t *source_to_fp) {

  source_from_fp->prev->next = source_to_fp->next;
  source_to_fp->next->prev = source_from_fp->prev;

  source_to_fp->next = dest_qp->next;
  dest_qp->next->prev = source_to_fp;
  dest_qp->next = source_from_fp;
  source_from_fp->prev = (iohdlc_frame_t *)dest_qp;
}

/** @} */

#endif /* IOHDLCQUEUE_H_ */

/** @} */
