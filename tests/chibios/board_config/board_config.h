/**
 * @file    board_config.h
 * @brief   Board-specific UART configuration for test suite.
 * @details Includes appropriate board configuration based on target platform.
 */

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

/* Include board-specific configuration */
#if defined(BOARD_BV1000GTV) || !defined(BOARD_NAME)
  /* Default to SAMA5D2x/BV1000GTV configuration */
  #include "board_sama5d2x.h"
#else
  #error "Board configuration not available for this target"
#endif

#endif /* BOARD_CONFIG_H */
