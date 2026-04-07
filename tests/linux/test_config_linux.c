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
 * @file    test_config_linux.c
 * @brief   Linux command-line configuration parser.
 */

#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

/*===========================================================================*/
/* Helper Functions                                                          */
/*===========================================================================*/

static void print_usage(const char *progname) {
  printf("Usage: %s [options]\n\n", progname);
  printf("Options:\n");
  printf("  --mode=MODE         Operating mode: nrm, abm (default: nrm)\n");
  printf("  --modulo=N          HDLC modulo: 8 or 128 (default: 8)\n");
  printf("  --twa               Use Two-Way Alternate (default: TWS)\n");
  printf("  --tws               Use Two-Way Simultaneous (explicit)\n");
  printf("  --count=N           Run for N iterations (default mode)\n");
  printf("  --time=N            Run for N seconds\n");
  printf("  --exchanges=N       Exchanges per iteration (default: 10)\n");
  printf("  --size=N            Packet size in bytes, header included (default: 64, range: %u-%u)\n",
         (unsigned)TEST_PACKET_HEADER_SIZE, (unsigned)TEST_EXCHANGE_MAX_PACKET_SIZE);
  printf("  --direction=DIR     Traffic direction: pri2sec, sec2pri, both (default: both)\n");
  printf("  --error-rate=N      Error injection rate 0-100%% (default: 0=disabled)\n");
  printf("  --reply-timeout=N   Reply timeout in ms (default: 0=100ms)\n");
  printf("  --poll-retry-max=N  Max poll retries before link down (default: 0=5)\n");
  printf("  --progress-interval=ms  Progress update interval in ms (default: 1000)\n");
  printf("  --watermark-delay=N Reader delay every 256 packets in ms (default: 0=disabled)\n");
  printf("  --krs=N             Window size (ks=kr=N, 1..modmask; default: modmask)\n");
  printf("  --help              Show this help\n\n");
  printf("Examples:\n");
  printf("  %s --mode=nrm --twa --count=100 --exchanges=50 --size=64\n", progname);
  printf("  %s --mode=nrm --tws --time=60 --direction=pri2sec --size=100\n", progname);
  printf("  %s --mode=abm --tws --modulo=128 --count=200\n", progname);
  printf("\n");
}

/*===========================================================================*/
/* Configuration Parser                                                      */
/*===========================================================================*/

bool test_parse_config(test_config_t *cfg, int argc, char **argv) {
  /* Default configuration */
  cfg->mode = IOHDLC_OM_NRM;
  cfg->use_twa = false;
  cfg->modulo = 8;
  cfg->duration_type = TEST_BY_COUNT;
  cfg->duration_value = 10;
  cfg->exchanges_per_iteration = 10;
  cfg->bytes_per_exchange = 64;
  cfg->traffic_direction = TRAFFIC_BIDIRECTIONAL;
  cfg->error_rate = 0;  /* Disabled by default */
  cfg->reply_timeout_ms = 0;  /* Use default (100ms) */
  cfg->poll_retry_max = 0;  /* Use default (5) */
  cfg->progress_interval_ms = 1000;  /* 1 second by default */
  cfg->watermark_delay_ms = 0;  /* Disabled by default */
  cfg->krs = 0;                 /* Use modmask default */
  cfg->test_name = argv[0];
  
  /* Long options */
  static struct option long_options[] = {
    {"mode",      required_argument, 0, 'm'},
    {"modulo",    required_argument, 0, 'M'},
    {"twa",       no_argument,       0, 'a'},
    {"tws",       no_argument,       0, 's'},
    {"count",     required_argument, 0, 'c'},
    {"time",      required_argument, 0, 't'},
    {"exchanges", required_argument, 0, 'e'},
    {"size",      required_argument, 0, 'z'},
    {"direction", required_argument, 0, 'd'},
    {"error-rate",required_argument, 0, 'r'},
    {"reply-timeout",required_argument, 0, 'T'},
    {"poll-retry-max",required_argument, 0, 'R'},
    {"progress-interval", required_argument, 0, 'p'},
    {"watermark-delay", required_argument, 0, 'w'},
    {"krs",       required_argument, 0, 'K'},
    {"help",      no_argument,       0, 'h'},
    {0, 0, 0, 0}
  };
  
  int opt;
  int option_index = 0;
  
  while ((opt = getopt_long(argc, argv, "m:M:asc:t:e:z:d:r:T:R:p:w:K:h",
                            long_options, &option_index)) != -1) {
    switch (opt) {
      case 'm':  /* --mode */
        if (strcmp(optarg, "nrm") == 0) {
          cfg->mode = IOHDLC_OM_NRM;
        } else if (strcmp(optarg, "abm") == 0) {
          cfg->mode = IOHDLC_OM_ABM;
        } else {
          fprintf(stderr, "Error: Invalid mode '%s'\n", optarg);
          return false;
        }
        break;

      case 'M':  /* --modulo */
        cfg->modulo = (uint16_t)atoi(optarg);
        if (cfg->modulo != 8 && cfg->modulo != 128) {
          fprintf(stderr, "Error: Invalid modulo '%s' (expected 8 or 128)\n", optarg);
          return false;
        }
        break;
        
      case 'a':  /* --twa */
        cfg->use_twa = true;
        break;
        
      case 's':  /* --tws */
        cfg->use_twa = false;
        break;
        
      case 'c':  /* --count */
        cfg->duration_type = TEST_BY_COUNT;
        cfg->duration_value = atoi(optarg);
        if (cfg->duration_value == 0) {
          fprintf(stderr, "Error: Invalid count value\n");
          return false;
        }
        break;
        
      case 't':  /* --time */
        cfg->duration_type = TEST_BY_TIME;
        cfg->duration_value = atoi(optarg);
        if (cfg->duration_value == 0) {
          fprintf(stderr, "Error: Invalid time value\n");
          return false;
        }
        break;
        
      case 'e':  /* --exchanges */
        cfg->exchanges_per_iteration = atoi(optarg);
        if (cfg->exchanges_per_iteration == 0) {
          fprintf(stderr, "Error: Invalid exchanges value\n");
          return false;
        }
        break;
        
      case 'z':  /* --size */ {
        int size = atoi(optarg);
        if (size < (int)TEST_PACKET_HEADER_SIZE ||
            size > (int)TEST_EXCHANGE_MAX_PACKET_SIZE) {
          fprintf(stderr, "Error: Invalid size value (must be %u-%u, header included)\n",
                  (unsigned)TEST_PACKET_HEADER_SIZE,
                  (unsigned)TEST_EXCHANGE_MAX_PACKET_SIZE);
          return false;
        }
        cfg->bytes_per_exchange = (uint32_t)size;
        break;
      }
        
      case 'd':  /* --direction */
        if (strcmp(optarg, "pri2sec") == 0) {
          cfg->traffic_direction = TRAFFIC_PRI_TO_SEC;
        } else if (strcmp(optarg, "sec2pri") == 0) {
          cfg->traffic_direction = TRAFFIC_SEC_TO_PRI;
        } else if (strcmp(optarg, "both") == 0) {
          cfg->traffic_direction = TRAFFIC_BIDIRECTIONAL;
        } else {
          fprintf(stderr, "Error: Invalid direction '%s'\n", optarg);
          return false;
        }
        break;
        
      case 'r':  /* --error-rate */
        cfg->error_rate = atoi(optarg);
        if (cfg->error_rate > 100) {
          fprintf(stderr, "Error: Invalid error rate (must be 0-100)\n");
          return false;
        }
        break;
      case 'p':  /* --progress-interval */
        cfg->progress_interval_ms = atoi(optarg);
        if (cfg->progress_interval_ms == 0) {
          fprintf(stderr, "Error: Invalid progress interval (must be > 0)\n");
          return false;
        }
        break;
      case 'T':  /* --reply-timeout */
        cfg->reply_timeout_ms = atoi(optarg);
        break;
        
      case 'R':  /* --poll-retry-max */
        cfg->poll_retry_max = atoi(optarg);
        break;
        
      case 'w':  /* --watermark-delay */
        cfg->watermark_delay_ms = atoi(optarg);
        break;

      case 'K':  /* --krs */
        cfg->krs = atoi(optarg);
        if (cfg->krs == 0) {
          fprintf(stderr, "Error: --krs must be >= 1\n");
          return false;
        }
        break;
        
      case 'h':  /* --help */
        print_usage(argv[0]);
        exit(0);
        
      default:
        print_usage(argv[0]);
        return false;
    }
  }
  
  return true;
}
