/*
    ChibiOS - Copyright (C) 2006..2025 Giovanni Di Sirio

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#ifndef MCUCONF_H
#define MCUCONF_H

/*
 * i.MX95 drivers configuration.
 * The following settings override the default settings present in
 * the various device driver implementation headers.
 * Note that the settings for each driver only have effect if the whole
 * driver is enabled in halconf.h.
 *
 * IRQ priorities:
 * 15...0       Lowest...Highest.
 */

#define IMX95_MCUCONF

/*===========================================================================*/
/* General settings.                                                         */
/*===========================================================================*/

/**
 * @brief   Initializes the low level driver.
 * @note    Setting this to @p TRUE skips low level initialization.
 */
#define IMX95_NO_INIT                       FALSE

/**
 * @brief   Enables a DDR no-cache area for DMA/shared buffers.
 */
#define IMX95_NOCACHE_ENABLE                TRUE

/**
 * @brief   Enables explicit DDR cache attributes.
 */
#define IMX95_DDR_CACHE_ENABLE              TRUE

/**
 * @brief   DDR cache policy used by this demo.
 */
#define IMX95_DDR_CACHE_ATTR                MPU_RASR_ATTR_CACHEABLE_WB_WA

/**
 * @brief   MPU region used for the DDR no-cache area.
 */
#define IMX95_NOCACHE_MPU_REGION            MPU_REGION_6

/**
 * @brief   Base address of the DDR no-cache area.
 */
#define IMX95_NOCACHE_RBAR                  0x80F00000U

/**
 * @brief   Size of the DDR no-cache area.
 */
#define IMX95_NOCACHE_RASR                  MPU_RASR_SIZE_1M

/*===========================================================================*/
/* HAL driver system settings.                                               */
/*===========================================================================*/

/**
 * @brief   SysTick clock source selection.
 * @details The current reference demo uses the periodic SysTick backend.
 *          Tickless operation will later move to a different timer source.
 */
#define IMX95_SYSTICK_SOURCE                IMX95_SYSTICK_CORECLK

/*===========================================================================*/
/* Clock system settings.                                                    */
/*===========================================================================*/

/**
 * @brief   LPUART3 clock parent selection.
 */
#define IMX95_LPUART3SEL                    IMX95_LPUART3SEL_OSC24M

/**
 * @brief   LPUART3 functional clock in Hertz.
 */
#define IMX95_LPUART3CLK_FREQUENCY          24000000U

/**
 * @brief   LPSPI6 clock parent selection.
 */
#define IMX95_LPSPI6SEL                     IMX95_LPSPI6SEL_SYSPLL1_PFD1_DIV2

/**
 * @brief   LPSPI6 functional clock in Hertz.
 */
#define IMX95_LPSPI6CLK_FREQUENCY           200000000U

/*===========================================================================*/
/* IRQ system settings.                                                      */
/*===========================================================================*/

/**
 * @brief   SysTick interrupt priority level.
 */
#define IMX95_ST_IRQ_PRIORITY               8U

/**
 * @brief   MU5_A interrupt priority level.
 */
#define IMX95_MU_IRQ_PRIORITY               3U

/*===========================================================================*/
/* EDMA helper driver settings.                                              */
/*===========================================================================*/

/**
 * @brief   Enables DMA5_2 support for the DDR DMA self-test.
 */
#define IMX95_EDMA_USE_DMA5_2               IMX95_SPI_USE_LPSPI6

/**
 * @brief   Enables verbose DMA self-test diagnostics.
 */
#define IMX95_EDMA_TEST_VERBOSE             FALSE

/**
 * @brief   DMA5_2 interrupt priority level.
 */
#define IMX95_EDMA_DMA5_2_IRQ_PRIORITY      7U

/*===========================================================================*/
/* SERIAL driver system settings.                                            */
/*===========================================================================*/

/**
 * @brief   LPUART3 driver enable switch.
 */
#define IMX95_SERIAL_USE_LPUART3            TRUE

/**
 * @brief   LPUART3 interrupt priority level.
 */
#define IMX95_SERIAL_LPUART3_PRIORITY       9U

/**
 * @brief   Number of bytes preloaded into the TX FIFO per service cycle.
 */
#define IMX95_SERIAL_FIFO_PRELOAD           16U

/*===========================================================================*/
/* SPI driver system settings.                                               */
/*===========================================================================*/

/**
 * @brief   LPSPI6 driver enable switch.
 */
#define IMX95_SPI_USE_LPSPI6                TRUE

/**
 * @brief   DMA5_2 RX channel assigned to LPSPI6.
 */
#define IMX95_SPI_LPSPI6_RX_DMA_CHANNEL     2U

/**
 * @brief   DMA5_2 TX channel assigned to LPSPI6.
 */
#define IMX95_SPI_LPSPI6_TX_DMA_CHANNEL     3U

#endif /* MCUCONF_H */
