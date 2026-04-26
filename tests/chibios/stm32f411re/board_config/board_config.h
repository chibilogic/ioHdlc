/*
 * ioHdlc test board mapping for the dedicated F411 frontend.
 */

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#if defined(BOARD_ST_NUCLEO64_F411RE)
  #include "board_nucleof411re.h"
#else
  #error "Board configuration not available for this target"
#endif

#endif /* BOARD_CONFIG_H */
