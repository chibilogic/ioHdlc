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
  qp->next = (iohdlc_frame_q_t *)qp;
  qp->prev = (iohdlc_frame_q_t *)qp;
}

/**
 * @brief   Check if the @p qp queue is empty.
 */
static inline bool ioHdlc_frameq_isempty(const iohdlc_frame_q_t *qp) {
  return (qp->next == (iohdlc_frame_q_t *)qp);
}

/**
 * @brief   Insert the frame queue header @p fqp into the @p qp frame queue.
 * @param   qp   Queue head.
 * @param   fqp  Queue header embedded in frame (e.g., &frame->q or &frame->q_aux).
 */
static inline void ioHdlc_frameq_insert(iohdlc_frame_q_t *qp, iohdlc_frame_q_t *fqp) {
  fqp->next = (iohdlc_frame_q_t *)qp;
  fqp->prev = qp->prev;
  fqp->prev->next = fqp;
  qp->prev = fqp;
}

/**
 * @brief   Remove a frame queue header from the @p qp queue in natural fifo order.
 * @return  Pointer to removed queue header (use IOHDLC_FRAME_FROM_Q or _Q_AUX to get frame).
 */
static inline iohdlc_frame_q_t *ioHdlc_frameq_remove(iohdlc_frame_q_t *qp) {
  iohdlc_frame_q_t *rfqp = qp->next;
  qp->next = rfqp->next;
  qp->next->prev = (iohdlc_frame_q_t *)qp;
  return rfqp;
}

/**
 * @brief   Remove a frame from the @p qp queue in natural fifo order and
 *          lookahead the next frame queue header in @p *next_fqp without removing it.
 * @return  Pointer to removed queue header.
 */
static inline iohdlc_frame_q_t *ioHdlc_frameq_remove_la(
  iohdlc_frame_q_t *qp,
  iohdlc_frame_q_t **next_fqp) {

  iohdlc_frame_q_t *rfqp = qp->next;
  qp->next = rfqp->next;
  qp->next->prev = (iohdlc_frame_q_t *)qp;
  if (next_fqp != NULL) {
    if (ioHdlc_frameq_isempty(qp))
      *next_fqp = NULL;
    else
      *next_fqp = qp->next;
  }
  return rfqp;
}

/**
 * @brief   Remove a frame queue header from the @p qp queue in lifo order.
 * @return  Pointer to removed queue header.
 */
static inline iohdlc_frame_q_t *ioHdlc_frameq_lifo_remove(iohdlc_frame_q_t *qp) {
  iohdlc_frame_q_t *rfqp = qp->prev;
  qp->prev = rfqp->prev;
  qp->prev->next = (iohdlc_frame_q_t *)qp;
  return rfqp;
}

/**
 * @brief   Delete the frame queue header @p fqp from its own queue.
 * @param   fqp  Queue header embedded in frame (e.g., &frame->q).
 */
static inline void ioHdlc_frameq_delete(iohdlc_frame_q_t *fqp) {
  fqp->prev->next = fqp->next;
  fqp->next->prev = fqp->prev;
}

/**
 * @brief   Move a set of consecutive frame queue headers [@p source_from_fqp, @p source_to_fqp]
 *          from their own queue to the head of a destination @p dest_qp queue
 */
static inline void ioHdlc_frameq_move(iohdlc_frame_q_t *dest_qp,
    iohdlc_frame_q_t *source_from_fqp, iohdlc_frame_q_t *source_to_fqp) {

  source_from_fqp->prev->next = source_to_fqp->next;
  source_to_fqp->next->prev = source_from_fqp->prev;

  source_to_fqp->next = dest_qp->next;
  dest_qp->next->prev = source_to_fqp;
  dest_qp->next = source_from_fqp;
  source_from_fqp->prev = (iohdlc_frame_q_t *)dest_qp;
}



/** @} */

#endif /* IOHDLCQUEUE_H_ */

/** @} */
