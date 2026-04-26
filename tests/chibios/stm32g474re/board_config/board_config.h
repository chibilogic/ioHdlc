/*
 * ioHdlc test board mapping for the dedicated G474 frontend.
 */

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#if defined(BOARD_ST_NUCLEO64_G474RE)
  #include "board_nucleog474re.h"
#else
  #error "Board configuration not available for this target"
#endif

#endif /* BOARD_CONFIG_H */
