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
 * @file    ioHdlc_log.c
 * @brief   HDLC frame logging implementation.
 */

#include "ioHdlc_log.h"

#if IOHDLC_LOG_LEVEL > IOHDLC_LOG_LEVEL_OFF

#include <stdio.h>
#include <stdarg.h>
#include "ioHdlcosal.h"

/*===========================================================================*/
/* Module local variables                                                    */
/*===========================================================================*/

/**
 * @brief   Runtime enable flag.
 */
bool iohdlc_log_enabled = true;

/*===========================================================================*/
/* Module local functions                                                    */
/*===========================================================================*/

/**
 * @brief   Get milliseconds since first log call.
 * @return  Milliseconds with 3 decimal places.
 */
static double get_timestamp_ms(void) {
  return iohdlc_osal_get_time_ms();
}

/**
 * @brief   Convert S-frame function to string.
 */
static const char* sfun_to_str(iohdlc_log_sfun_t fun) {
  switch (fun) {
    case IOHDLC_LOG_RR:   return "RR";
    case IOHDLC_LOG_RNR:  return "RNR";
    case IOHDLC_LOG_REJ:  return "REJ";
    case IOHDLC_LOG_SREJ: return "SREJ";
    default:              return "???";
  }
}

/**
 * @brief   Convert U-frame function to string.
 */
static const char* ufun_to_str(iohdlc_log_ufun_t fun) {
  switch (fun) {
    case IOHDLC_LOG_SNRM: return "SNRM";
    case IOHDLC_LOG_SARM: return "SARM";
    case IOHDLC_LOG_SABM: return "SABM";
    case IOHDLC_LOG_DISC: return "DISC";
    case IOHDLC_LOG_UA:   return "UA";
    case IOHDLC_LOG_DM:   return "DM";
    case IOHDLC_LOG_FRMR: return "FRMR";
    default:              return "???";
  }
}

/*===========================================================================*/
/* Module exported functions                                                 */
/*===========================================================================*/

/**
 * @brief   Log an I-frame.
 * @note    Format: [time] <S/R><saddr> A<addr> I<ns>,<nr> <P/F> len=<len> w=<pend>/<win> [flags]
 *          Example: [001.234] S1 A2 I0,0 P len=40 w=1/7
 */
void iohdlc_log_iframe(iohdlc_log_dir_t dir, uint8_t saddr, uint8_t addr,
                        uint32_t ns, uint32_t nr, bool pf, size_t len,
                        uint32_t pending, uint32_t window, uint8_t flags) {
  if (!iohdlc_log_enabled)
    return;
  
  double ts = get_timestamp_ms();
  
  /* Determine if P or F based on address and direction:
     TX: saddr == addr → response (F), otherwise command (P)
     RX: saddr != addr → response (F), otherwise command (P) */
  bool is_final = (dir == IOHDLC_LOG_TX) ? (saddr == addr) : (saddr != addr);
  
  IOHDLC_OSAL_PRINTF("[%07.3f] %c%u A%u I%u,%u %c len=%zu w=%u/%u",
             ts,
             dir == IOHDLC_LOG_TX ? 'S' : 'R',
             saddr,
             addr,
             ns,
             nr,
             pf ? (is_final ? 'F' : 'P') : '-',
             len,
             pending,
             window);
  
  /* Append optional flags */
  if (flags & IOHDLC_LOG_FLAG_RETX)
    IOHDLC_OSAL_PRINTF(" [RETX]");
  if (flags & IOHDLC_LOG_FLAG_REJ)
    IOHDLC_OSAL_PRINTF(" [REJ]");
  
  IOHDLC_OSAL_PRINTF("\n");
}

/**
 * @brief   Log an S-frame.
 * @note    Format: [time] <S/R><saddr> A<addr> <fun><nr> <P/F> [flags]
 *          Example: [001.240] S2 A2 RR1 F
 */
void iohdlc_log_sframe(iohdlc_log_dir_t dir, uint8_t saddr, uint8_t addr,
                        iohdlc_log_sfun_t fun, uint32_t nr, bool pf,
                        uint32_t pending, uint8_t flags) {
  if (!iohdlc_log_enabled)
    return;
  
  double ts = get_timestamp_ms();
  
  /* Determine if P or F based on address and direction:
     TX: saddr == addr → response (F), otherwise command (P)
     RX: saddr != addr → response (F), otherwise command (P) */
  bool is_final = (dir == IOHDLC_LOG_TX) ? (saddr == addr) : (saddr != addr);
  
  IOHDLC_OSAL_PRINTF("[%07.3f] %c%u A%u %s%u %c w=%u",
            ts,
            dir == IOHDLC_LOG_TX ? 'S' : 'R',
            saddr,
            addr,
            sfun_to_str(fun),
            nr,
            pf ? (is_final ? 'F' : 'P') : '-',
            pending);
  
  /* Append optional flags */
  if (flags & IOHDLC_LOG_FLAG_BUSY)
    IOHDLC_OSAL_PRINTF(" [BUSY]");
  if (flags & IOHDLC_LOG_FLAG_REJ)
    IOHDLC_OSAL_PRINTF(" [REJ]");
  
  IOHDLC_OSAL_PRINTF("\n");
}

/**
 * @brief   Log a U-frame.
 * @note    Format: [time] <S/R><saddr> A<addr> <fun> <P/F>
 *          Example: [001.300] S1 A1 SNRM P
 */
void iohdlc_log_uframe(iohdlc_log_dir_t dir, uint8_t saddr, uint8_t addr,
                        iohdlc_log_ufun_t fun, bool pf) {
  if (!iohdlc_log_enabled)
    return;
  
  double ts = get_timestamp_ms();
  
  /* Determine if P or F based on address and direction:
     TX: saddr == addr → response (F), otherwise command (P)
     RX: saddr != addr → response (F), otherwise command (P) */
  bool is_final = (dir == IOHDLC_LOG_TX) ? (saddr == addr) : (saddr != addr);
  
  IOHDLC_OSAL_PRINTF("[%07.3f] %c%u A%u %s %c\n",
             ts,
             dir == IOHDLC_LOG_TX ? 'S' : 'R',
             saddr,
             addr,
             ufun_to_str(fun),
             pf ? (is_final ? 'F' : 'P') : '-');
}

void iohdlc_log_msg(iohdlc_log_dir_t dir, uint8_t saddr, const char *msg, ...) {
  if (!iohdlc_log_enabled)
    return;
  
  double ts = get_timestamp_ms();
  
  IOHDLC_OSAL_PRINTF("[%07.3f] %c%u ",
    ts,
    dir == IOHDLC_LOG_TX ? 'S' : 'R',
    saddr);
  
  va_list args;
  va_start(args, msg);
  IOHDLC_OSAL_VPRINTF(msg, args);
  va_end(args);
  
  IOHDLC_OSAL_PRINTF("\n"); 
}

#endif /* IOHDLC_LOG_LEVEL > OFF */
