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
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

/*===========================================================================*/
/* Helper Functions                                                          */
/*===========================================================================*/

static bool parse_u32_arg(const char *value, uint32_t *out) {
  char *end;
  unsigned long parsed;

  if (value == NULL || *value == '\0' || *value == '-')
    return false;

  errno = 0;
  parsed = strtoul(value, &end, 0);
  if (errno != 0 || end == value || *end != '\0' || parsed > UINT32_MAX)
    return false;

  *out = (uint32_t)parsed;
  return true;
}

static void print_usage(const char *progname) {
  printf("Usage: %s [options]\n\n", progname);
  printf("Options:\n");
  printf("  --mode=MODE         Operating mode: nrm, abm (default: nrm)\n");
  printf("  --modulo=N          HDLC modulo: 8 or 128 (default: 8)\n");
  printf("  --twa               Use Two-Way Alternate (default: TWS)\n");
  printf("  --tws               Use Two-Way Simultaneous (explicit)\n");
  printf("  --count=N           Run for N iterations (default mode)\n");
  printf("  --time=N            Run for N seconds\n");
  printf("  --test[=N]          Run N TEST command/response cycles (default: 1)\n");
  printf("  --exchanges=N       Exchanges per iteration (default: 10)\n");
  printf("  --size=N            Packet size in bytes, header included (default: 64, range: %u-%u)\n",
         (unsigned)TEST_PACKET_HEADER_SIZE, (unsigned)TEST_EXCHANGE_MAX_PACKET_SIZE);
  printf("  --rand-size=N       Random packet-size seed (range: %u-%u, alias: --rand_size=N)\n",
         (unsigned)TEST_EXCHANGE_RAND_SIZE_MIN,
         (unsigned)TEST_EXCHANGE_RAND_SIZE_MAX);
  printf("  --direction=DIR     Traffic direction: both, a2b, b2a (aliases: pri2sec, sec2pri)\n");
  printf("  --endpoint=EP       Local endpoint: both, a, b (aliases: primary, secondary)\n");
  printf("  --error-rate=N      Error injection rate 0-100%% (default: 0=disabled)\n");
  printf("  --reply-timeout=N   Reply timeout in ms (default: 0=library default %u)\n",
         (unsigned)IOHDLC_REPLY_TIMEOUT_MS_DEFAULT);
  printf("  --idle-poll-timeout=N NRM idle-poll T3 in ms (default: 0=2*T1, range: T1-%u)\n",
         (unsigned)UINT16_MAX);
  printf("  --poll-retry-max=N  Max poll retries 0-%u (default: 0=auto around 25s total)\n",
         (unsigned)TEST_POLL_RETRY_MAX_LIMIT);
  printf("  --progress-interval=ms  Progress update interval in ms (default: 1000)\n");
  printf("  --watermark-delay=N Reader delay every 256 packets in ms (default: 0=disabled)\n");
  printf("  --krs=N             Window size (ks=kr=N, 1..modmask; default: modmask)\n");
  printf("  --help              Show this help\n\n");
  printf("Examples:\n");
  printf("  %s --mode=nrm --twa --count=100 --exchanges=50 --size=64\n", progname);
  printf("  %s --mode=nrm --tws --time=60 --direction=pri2sec --size=100\n", progname);
  printf("  %s --mode=abm --tws --modulo=128 --count=200\n", progname);
  printf("  %s --test=10 --size=64\n", progname);
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
  cfg->rand_size_enabled = false;
  cfg->rand_size_seed = 0;
  cfg->test_command = false;
  cfg->test_command_count = 0;
  cfg->traffic_direction = TRAFFIC_BIDIRECTIONAL;
  cfg->endpoint_mode = TEST_ENDPOINT_BOTH;
  cfg->error_rate = 0;  /* Disabled by default */
  cfg->reply_timeout_ms = 0;  /* Use default (100ms) */
  cfg->idle_poll_timeout_ms = 0;  /* Use default (2*T1) */
  cfg->poll_retry_max = 0;  /* Auto from reply-timeout */
  cfg->poll_retry_max_auto = false;
  cfg->poll_retry_total_timeout_ms = 0;
  cfg->progress_interval_ms = 1000;  /* 1 second by default */
  cfg->watermark_delay_ms = 0;  /* Disabled by default */
  cfg->krs = 0;                 /* Use modmask default */
  cfg->test_name = argv[0];
  bool exchange_option_seen = false;
  
  /* Long options */
  static struct option long_options[] = {
    {"mode",      required_argument, 0, 'm'},
    {"modulo",    required_argument, 0, 'M'},
    {"twa",       no_argument,       0, 'a'},
    {"tws",       no_argument,       0, 's'},
    {"count",     required_argument, 0, 'c'},
    {"time",      required_argument, 0, 't'},
    {"test",      optional_argument, 0, 'x'},
    {"exchanges", required_argument, 0, 'e'},
    {"size",      required_argument, 0, 'z'},
    {"rand-size", required_argument, 0, 'Z'},
    {"rand_size", required_argument, 0, 'Z'},
    {"direction", required_argument, 0, 'd'},
    {"endpoint",  required_argument, 0, 'E'},
    {"error-rate",required_argument, 0, 'r'},
    {"reply-timeout",required_argument, 0, 'T'},
    {"idle-poll-timeout",required_argument, 0, 'I'},
    {"poll-retry-max",required_argument, 0, 'R'},
    {"progress-interval", required_argument, 0, 'p'},
    {"watermark-delay", required_argument, 0, 'w'},
    {"krs",       required_argument, 0, 'K'},
    {"help",      no_argument,       0, 'h'},
    {0, 0, 0, 0}
  };
  
  int opt;
  int option_index = 0;
  
  while ((opt = getopt_long(argc, argv, "m:M:asc:t:e:z:d:E:r:T:I:R:p:w:K:h",
                            long_options, &option_index)) != -1) {
    switch (opt) {
      case 'm':  /* --mode */
        exchange_option_seen = true;
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
        exchange_option_seen = true;
        cfg->modulo = (uint16_t)atoi(optarg);
        if (cfg->modulo != 8 && cfg->modulo != 128) {
          fprintf(stderr, "Error: Invalid modulo '%s' (expected 8 or 128)\n", optarg);
          return false;
        }
        break;
        
      case 'a':  /* --twa */
        exchange_option_seen = true;
        cfg->use_twa = true;
        break;
        
      case 's':  /* --tws */
        exchange_option_seen = true;
        cfg->use_twa = false;
        break;
        
      case 'c':  /* --count */
        exchange_option_seen = true;
        cfg->duration_type = TEST_BY_COUNT;
        cfg->duration_value = atoi(optarg);
        if (cfg->duration_value == 0) {
          fprintf(stderr, "Error: Invalid count value\n");
          return false;
        }
        break;
        
      case 't':  /* --time */
        exchange_option_seen = true;
        cfg->duration_type = TEST_BY_TIME;
        cfg->duration_value = atoi(optarg);
        if (cfg->duration_value == 0) {
          fprintf(stderr, "Error: Invalid time value\n");
          return false;
        }
        break;
        
      case 'x': {  /* --test[=N] */
        uint32_t count = 1U;

        if (optarg != NULL && !parse_u32_arg(optarg, &count)) {
          fprintf(stderr, "Error: Invalid TEST cycle count '%s'\n", optarg);
          return false;
        }
        if (count == 0U) {
          fprintf(stderr, "Error: --test count must be > 0\n");
          return false;
        }
        cfg->test_command = true;
        cfg->test_command_count = count;
        break;
      }

      case 'e':  /* --exchanges */
        exchange_option_seen = true;
        cfg->exchanges_per_iteration = atoi(optarg);
        if (cfg->exchanges_per_iteration == 0) {
          fprintf(stderr, "Error: Invalid exchanges value\n");
          return false;
        }
        break;
        
      case 'z':  /* --size */ {
        uint32_t size;

        if (!parse_u32_arg(optarg, &size) ||
            size > TEST_EXCHANGE_MAX_PACKET_SIZE) {
          fprintf(stderr, "Error: Invalid size value (must be 0-%u for TEST or %u-%u for exchange)\n",
                  (unsigned)TEST_EXCHANGE_MAX_PACKET_SIZE,
                  (unsigned)TEST_PACKET_HEADER_SIZE,
                  (unsigned)TEST_EXCHANGE_MAX_PACKET_SIZE);
          return false;
        }
        cfg->bytes_per_exchange = size;
        break;
      }

      case 'Z':  /* --rand-size */ {
        uint32_t seed;

        exchange_option_seen = true;
        if (!parse_u32_arg(optarg, &seed)) {
          fprintf(stderr, "Error: Invalid rand-size seed '%s'\n", optarg);
          return false;
        }
        cfg->rand_size_enabled = true;
        cfg->rand_size_seed = seed;
        break;
      }
        
      case 'd':  /* --direction */
        exchange_option_seen = true;
        if (strcmp(optarg, "pri2sec") == 0 || strcmp(optarg, "a2b") == 0) {
          cfg->traffic_direction = TRAFFIC_PRI_TO_SEC;
        } else if (strcmp(optarg, "sec2pri") == 0 || strcmp(optarg, "b2a") == 0) {
          cfg->traffic_direction = TRAFFIC_SEC_TO_PRI;
        } else if (strcmp(optarg, "both") == 0) {
          cfg->traffic_direction = TRAFFIC_BIDIRECTIONAL;
        } else {
          fprintf(stderr, "Error: Invalid direction '%s'\n", optarg);
          return false;
        }
        break;

      case 'E':  /* --endpoint */
        if (strcmp(optarg, "both") == 0) {
          cfg->endpoint_mode = TEST_ENDPOINT_BOTH;
        } else if (strcmp(optarg, "a") == 0 || strcmp(optarg, "primary") == 0) {
          cfg->endpoint_mode = TEST_ENDPOINT_A;
        } else if (strcmp(optarg, "b") == 0 || strcmp(optarg, "secondary") == 0) {
          cfg->endpoint_mode = TEST_ENDPOINT_B;
        } else {
          fprintf(stderr, "Error: Invalid endpoint '%s'\n", optarg);
          return false;
        }
        break;
        
      case 'r':  /* --error-rate */
        exchange_option_seen = true;
        cfg->error_rate = atoi(optarg);
        if (cfg->error_rate > 100) {
          fprintf(stderr, "Error: Invalid error rate (must be 0-100)\n");
          return false;
        }
        break;
      case 'p':  /* --progress-interval */
        exchange_option_seen = true;
        cfg->progress_interval_ms = atoi(optarg);
        if (cfg->progress_interval_ms == 0) {
          fprintf(stderr, "Error: Invalid progress interval (must be > 0)\n");
          return false;
        }
        break;
      case 'T':  /* --reply-timeout */
        exchange_option_seen = true;
        cfg->reply_timeout_ms = atoi(optarg);
        break;

      case 'I': {  /* --idle-poll-timeout */
        uint32_t timeout;

        exchange_option_seen = true;
        if (!parse_u32_arg(optarg, &timeout) || timeout > UINT16_MAX) {
          fprintf(stderr, "Error: Invalid idle poll timeout (must be 0-%u)\n",
                  (unsigned)UINT16_MAX);
          return false;
        }
        cfg->idle_poll_timeout_ms = timeout;
        break;
      }
        
      case 'R': {  /* --poll-retry-max */
        int retries = atoi(optarg);
        exchange_option_seen = true;
        if (retries < 0 || retries > (int)TEST_POLL_RETRY_MAX_LIMIT) {
          fprintf(stderr, "Error: Invalid poll retry max (must be 0-%u)\n",
                  (unsigned)TEST_POLL_RETRY_MAX_LIMIT);
          return false;
        }
        cfg->poll_retry_max = (uint8_t)retries;
        break;
      }
        
      case 'w':  /* --watermark-delay */
        exchange_option_seen = true;
        cfg->watermark_delay_ms = atoi(optarg);
        break;

      case 'K':  /* --krs */
        exchange_option_seen = true;
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

  for (int i = optind; i < argc; i++) {
    uint32_t seed;
    const char *value = NULL;

    if (cfg->test_command) {
      fprintf(stderr, "Error: Argument '%s' is not valid with --test\n", argv[i]);
      return false;
    }

    if (strncmp(argv[i], "rand_size=", 10) == 0) {
      value = argv[i] + 10;
    } else if (strncmp(argv[i], "rand-size=", 10) == 0) {
      value = argv[i] + 10;
    }

    if (value == NULL) {
      fprintf(stderr, "Error: Unknown argument '%s'\n", argv[i]);
      return false;
    }

    if (!parse_u32_arg(value, &seed)) {
      fprintf(stderr, "Error: Invalid rand-size seed '%s'\n", value);
      return false;
    }

    cfg->rand_size_enabled = true;
    cfg->rand_size_seed = seed;
  }

  if (!cfg->test_command &&
      cfg->bytes_per_exchange < TEST_PACKET_HEADER_SIZE) {
    fprintf(stderr, "Error: Invalid size value (must be %u-%u, header included)\n",
            (unsigned)TEST_PACKET_HEADER_SIZE,
            (unsigned)TEST_EXCHANGE_MAX_PACKET_SIZE);
    return false;
  }

  if (cfg->test_command) {
    if (exchange_option_seen) {
      fprintf(stderr, "Error: --test accepts only --size and --endpoint\n");
      return false;
    }
  }
  
  return true;
}
