/*
 * ioHdlc Exchange Test - Linux Runner
 * Copyright (C) 2024 Isidoro Orabona
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
/**
 * @file    test_runner_exchange.c
 * @brief   Linux entry point for exchange test.
 */

/* Entry point from test_exchange.c */
extern int test_exchange_main(int argc, char **argv);

int main(int argc, char **argv) {
  return test_exchange_main(argc, argv);
}
