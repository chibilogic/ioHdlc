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
 * @file    test_scenarios.h
 * @brief   OS-agnostic test scenario declarations.
 *
 * @details Declares test functions that can be executed by platform-specific
 *          test runners. All test scenarios are OS-agnostic and portable.
 */

#ifndef TEST_SCENARIOS_H_
#define TEST_SCENARIOS_H_

#include <stdbool.h>
#include "adapter_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================*/
/* Test: Frame Pool                                                          */
/*===========================================================================*/

int test_pool_init(void);
int test_take_release(void);
int test_addref(void);
int test_watermark(void);
int test_exhaust_pool(void);

/*===========================================================================*/
/* Test: Basic Connection                                                    */
/*===========================================================================*/

bool test_station_creation(void);
bool test_peer_creation(void);
bool test_snrm_handshake(const test_adapter_t *adapter);
bool test_data_exchange(const test_adapter_t *adapter);

/*===========================================================================*/
/* Test: Basic Connection TWA                                                */
/*===========================================================================*/

bool test_data_exchange_twa(const test_adapter_t *adapter);

bool test_data_exchange_twa(const test_adapter_t *adapter);

/*===========================================================================*/
/* Test: Basic Connection ABM                                                */
/*===========================================================================*/

bool test_abm_data_exchange(const test_adapter_t *adapter);

/*===========================================================================*/
/* Test: Checkpoint TWS                                                      */
/*===========================================================================*/

bool test_A1_1_frame_loss_window_full(const test_adapter_t *adapter);
bool test_A2_1_multiple_frame_loss(const test_adapter_t *adapter);
bool test_A2_2_first_and_last_frame_loss(const test_adapter_t *adapter);

/*===========================================================================*/
/* Test: Checkpoint TWA                                                      */
/*===========================================================================*/

bool test_A1_1_frame_loss_window_full_twa(const test_adapter_t *adapter);
bool test_A2_1_multiple_frame_loss_twa(const test_adapter_t *adapter);
bool test_A2_2_first_and_last_frame_loss_twa(const test_adapter_t *adapter);
bool test_checkpoint_with_rej_twa(void);
bool test_checkpoint_window_full_twa(void);

/*===========================================================================*/
/* Test: FRMR                                                                */
/*===========================================================================*/

bool test_frmr_invalid_nr(const test_adapter_t *adapter);

/*===========================================================================*/
/* Test: Multipoint NRM                                                      */
/*===========================================================================*/

int test_multipoint_connect_two_secondaries(void);
int test_multipoint_data_exchange(void);
int test_multipoint_selective_disconnect(void);
int test_multipoint_address_isolation(void);

#ifdef __cplusplus
}
#endif

#endif /* TEST_SCENARIOS_H_ */
