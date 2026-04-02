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
 * @file    test_config_shell.c
 * @brief   ChibiOS shell runtime configuration parser.
 * @details Parses command-line arguments from shell without getopt.
 *          Provides Linux-compatible CLI interface.
 */

#include "test_framework.h"
#include "test_helpers.h"
#include "chprintf.h"
#include <string.h>
#include <stdlib.h>

/*===========================================================================*/
/* Helper Functions                                                          */
/*===========================================================================*/

/**
 * @brief   Check if argument starts with prefix.
 */
static bool arg_starts_with(const char *arg, const char *prefix) {
  return strncmp(arg, prefix, strlen(prefix)) == 0;
}

/**
 * @brief   Extract value after '=' in --option=value format.
 */
static const char *get_arg_value(const char *arg) {
  const char *eq = strchr(arg, '=');
  return (eq != NULL) ? (eq + 1) : NULL;
}

/**
 * @brief   Print usage help to test output.
 */
static void print_usage(void) {
  test_printf("\r\n");
  test_printf("Usage: exchange [options]\r\n\r\n");
  test_printf("Options:\r\n");
  test_printf("  --size=N            Frame size in bytes (default: 64, max: 120)\r\n");
  test_printf("  --count=N           Run for N iterations (default: 100)\r\n");
  test_printf("  --exchanges=N       Exchanges per iteration (default: 10)\r\n");
  test_printf("  --modulo=N          HDLC modulo: 8 or 128 (default: 8)\r\n");
  test_printf("  -p N                Progress interval in ms (default: 1000)\r\n");
  test_printf("  -w N                Watermark delay every 256 packets in ms (default: 0)\r\n");
  test_printf("  --error-rate=N      Error rate 0-100%% (default: 0)\r\n");
  test_printf("  --direction=DIR     Direction: both|pri2sec|sec2pri (aliases: a2b|b2a)\r\n");
  test_printf("  --reply-timeout=N   Reply timeout in ms (default: 100)\r\n");
  test_printf("  --mode=MODE         Mode: nrm|abm (default: nrm)\r\n");
  test_printf("  --twa               Use Two-Way Alternate\r\n");
  test_printf("  --tws               Use Two-Way Simultaneous (default)\r\n");
  test_printf("  --time=N            Run for N seconds (vs --count)\r\n");
  test_printf("  --watermark-delay=N Reader delay every 256 packets in ms (default: 0)\r\n");
  test_printf("  --krs=N             Window size (ks=kr=N, >=1; default: modmask)\r\n");
  test_printf("  --help              Show this help\r\n");
  test_printf("\r\n");
  test_printf("Examples:\r\n");
  test_printf("  exchange --size=120 --count=50 --exchanges=100\r\n");
  test_printf("  exchange --direction=a2b --error-rate=5 -p 200\r\n");
  test_printf("  exchange --mode=abm --tws --modulo=128 --count=200\r\n");
  test_printf("\r\n");
}

/*===========================================================================*/
/* Configuration Parser                                                      */
/*===========================================================================*/

/**
 * @brief   Parse test configuration from shell command-line arguments.
 * @details Manual parsing without getopt (not available on bare-metal).
 *
 * @param[out] cfg      Configuration structure to fill
 * @param[in] argc      Argument count
 * @param[in] argv      Argument vector
 * @return              true on success, false on parse error
 */
bool test_parse_config(test_config_t *cfg, int argc, char **argv) {
  /* Default configuration */
  cfg->mode = IOHDLC_OM_NRM;
  cfg->use_twa = false;
  cfg->modulo = 8;
  cfg->duration_type = TEST_BY_COUNT;
  cfg->duration_value = 100;
  cfg->exchanges_per_iteration = 10;
  cfg->bytes_per_exchange = 64;
  cfg->traffic_direction = TRAFFIC_BIDIRECTIONAL;
  cfg->error_rate = 0;
  cfg->reply_timeout_ms = 100;      /* Reply timeout default: 100ms */
  cfg->poll_retry_max = 5;
  cfg->progress_interval_ms = 1000; /* Progress update default: 1000ms */
  cfg->watermark_delay_ms = 0;      /* Watermark delay disabled by default */
  cfg->krs = 0;                     /* Use modmask default */
  cfg->test_name = "Shell Exchange Test";
  
  /* Parse arguments */
  for (int i = 0; i < argc; i++) {
    const char *arg = argv[i];
    const char *value;
    
    /* --size=N */
    if (arg_starts_with(arg, "--size=")) {
      value = get_arg_value(arg);
      if (value) {
        int size = atoi(value);
        if (size > 0 && size <= 120) {
          cfg->bytes_per_exchange = size;
        } else {
          test_printf("Error: Invalid size (must be 1-120)\r\n");
          return false;
        }
      }
    }
    /* --count=N */
    else if (arg_starts_with(arg, "--count=")) {
      value = get_arg_value(arg);
      if (value) {
        int count = atoi(value);
        if (count > 0) {
          cfg->duration_type = TEST_BY_COUNT;
          cfg->duration_value = count;
        } else {
          test_printf("Error: Invalid count\r\n");
          return false;
        }
      }
    }
    /* --exchanges=N */
    else if (arg_starts_with(arg, "--exchanges=")) {
      value = get_arg_value(arg);
      if (value) {
        int exchanges = atoi(value);
        if (exchanges > 0) {
          cfg->exchanges_per_iteration = exchanges;
        } else {
          test_printf("Error: Invalid exchanges\r\n");
          return false;
        }
      }
    }
    /* --modulo=N */
    else if (arg_starts_with(arg, "--modulo=")) {
      value = get_arg_value(arg);
      if (value) {
        int modulo = atoi(value);
        if (modulo == 8 || modulo == 128) {
          cfg->modulo = (uint16_t)modulo;
        } else {
          test_printf("Error: Invalid modulo (8|128)\r\n");
          return false;
        }
      }
    }
    /* -p N (poll interval) */
    else if (strcmp(arg, "-p") == 0 && i + 1 < argc) {
      int interval = atoi(argv[++i]);
      if (interval > 0) {
        cfg->progress_interval_ms = interval;
      } else {
        test_printf("Error: Invalid poll interval\r\n");
        return false;
      }
    }
    /* -w N (watermark delay) */
    else if (strcmp(arg, "-w") == 0 && i + 1 < argc) {
      int delay = atoi(argv[++i]);
      if (delay >= 0) {
        cfg->watermark_delay_ms = delay;
      } else {
        test_printf("Error: Invalid watermark delay\r\n");
        return false;
      }
    }
    /* --error-rate=N */
    else if (arg_starts_with(arg, "--error-rate=")) {
      value = get_arg_value(arg);
      if (value) {
        int rate = atoi(value);
        if (rate >= 0 && rate <= 100) {
          cfg->error_rate = rate;
        } else {
          test_printf("Error: Invalid error rate (0-100)\r\n");
          return false;
        }
      }
    }
    /* --direction=DIR */
    else if (arg_starts_with(arg, "--direction=")) {
      value = get_arg_value(arg);
      if (value) {
        if (strcmp(value, "both") == 0) {
          cfg->traffic_direction = TRAFFIC_BIDIRECTIONAL;
        } else if (strcmp(value, "a2b") == 0 || strcmp(value, "pri2sec") == 0) {
          cfg->traffic_direction = TRAFFIC_PRI_TO_SEC;
        } else if (strcmp(value, "b2a") == 0 || strcmp(value, "sec2pri") == 0) {
          cfg->traffic_direction = TRAFFIC_SEC_TO_PRI;
        } else {
          test_printf("Error: Invalid direction (both|a2b|b2a)\r\n");
          return false;
        }
      }
    }
    /* --reply-timeout=N */
    else if (arg_starts_with(arg, "--reply-timeout=")) {
      value = get_arg_value(arg);
      if (value) {
        int timeout = atoi(value);
        if (timeout >= 0) {
          cfg->reply_timeout_ms = timeout;
        } else {
          test_printf("Error: Invalid reply timeout\r\n");
          return false;
        }
      }
    }
    /* --mode=MODE */
    else if (arg_starts_with(arg, "--mode=")) {
      value = get_arg_value(arg);
      if (value) {
        if (strcmp(value, "nrm") == 0) {
          cfg->mode = IOHDLC_OM_NRM;
        } else if (strcmp(value, "abm") == 0) {
          cfg->mode = IOHDLC_OM_ABM;
        } else {
          test_printf("Error: Invalid mode (nrm|abm)\r\n");
          return false;
        }
      }
    }
    /* --twa */
    else if (strcmp(arg, "--twa") == 0) {
      cfg->use_twa = true;
    }
    /* --tws */
    else if (strcmp(arg, "--tws") == 0) {
      cfg->use_twa = false;
    }
    /* --time=N */
    else if (arg_starts_with(arg, "--time=")) {
      value = get_arg_value(arg);
      if (value) {
        int time_sec = atoi(value);
        if (time_sec > 0) {
          cfg->duration_type = TEST_BY_TIME;
          cfg->duration_value = time_sec;
        } else {
          test_printf("Error: Invalid time\r\n");
          return false;
        }
      }
    }
    /* --poll-retry-max=N */
    else if (arg_starts_with(arg, "--poll-retry-max=")) {
      value = get_arg_value(arg);
      if (value) {
        int retries = atoi(value);
        if (retries >= 0) {
          cfg->poll_retry_max = retries;
        }
      }
    }
    /* --watermark-delay=N */
    else if (arg_starts_with(arg, "--watermark-delay=")) {
      value = get_arg_value(arg);
      if (value) {
        int delay = atoi(value);
        if (delay >= 0) {
          cfg->watermark_delay_ms = delay;
        } else {
          test_printf("Error: Invalid watermark delay\r\n");
          return false;
        }
      }
    }
    /* --krs=N */
    else if (arg_starts_with(arg, "--krs=")) {
      value = get_arg_value(arg);
      if (value) {
        int krs = atoi(value);
        if (krs >= 1) {
          cfg->krs = krs;
        } else {
          test_printf("Error: --krs must be >= 1\r\n");
          return false;
        }
      }
    }
    /* --help */
    else if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
      print_usage();
      return false;
    }
  }
  
  return true;
}
