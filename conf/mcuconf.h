/*
    ChibiOS - Copyright (C) 2006..2018 Giovanni Di Sirio

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

#define SAMA5D2x_MCUCONF

/*
 * HAL driver system settings.
 */
#define SAMA_HAL_IS_SECURE                  TRUE
#define SAMA_NO_INIT                        TRUE
#define SAMA_MOSCRC_ENABLED                 FALSE
#define SAMA_MOSCXT_ENABLED                 TRUE
#define SAMA_MOSC_SEL                       SAMA_MOSC_MOSCXT
#define SAMA_OSC_SEL                        SAMA_OSC_OSCXT
#define SAMA_MCK_SEL                        SAMA_MCK_PLLA_CLK
#define SAMA_MCK_PRES_VALUE                 1
#define SAMA_MCK_MDIV_VALUE                 3
#define SAMA_PLLA_MUL_VALUE                 41
#define SAMA_PLLADIV2_EN                    TRUE
#define SAMA_H64MX_H32MX_RATIO              2

/*
 * UART clock settings
 */
#define SAMA_UART0_USE_GCLK                 TRUE
#define SAMA_UART0_GCLK_SOURCE              SAMA_GCLK_MCK_CLK
#define SAMA_UART0_GCLK_DIV                 11
#define SAMA_UART1_USE_GCLK                 TRUE
#define SAMA_UART1_GCLK_SOURCE              SAMA_GCLK_MCK_CLK
#define SAMA_UART1_GCLK_DIV                 11
#define SAMA_UART2_USE_GCLK                 TRUE
#define SAMA_UART2_GCLK_SOURCE              SAMA_GCLK_MCK_CLK
#define SAMA_UART2_GCLK_DIV                 11
#define SAMA_UART3_USE_GCLK                 TRUE
#define SAMA_UART3_GCLK_SOURCE              SAMA_GCLK_MCK_CLK
#define SAMA_UART3_GCLK_DIV                 11
#define SAMA_UART4_USE_GCLK                 TRUE
#define SAMA_UART4_GCLK_SOURCE              SAMA_GCLK_MCK_CLK
#define SAMA_UART4_GCLK_DIV                 11

/*
 * AESB system settings
 */
#define SAMA_USE_AESB                       FALSE

/*
 * CAN driver system settings.
 */
#define SAMA_CAN_USE_CAN0                   FALSE
#define SAMA_CAN0_USE_GCLK                  FALSE
#define SAMA_CAN0_GCLK_SOURCE               SAMA_GCLKUPLL_CLK
#define SAMA_CAN0_GCLK_DIV                  60
#define SAMA_CAN_CAN0_IRQ_PRIORITY          6

#define SAMA_CAN_USE_CAN1                   FALSE
#define SAMA_CAN1_USE_GCLK                  FALSE
#define SAMA_CAN1_GCLK_SOURCE               SAMA_GCLKUPLL_CLK
#define SAMA_CAN1_GCLK_DIV                  60
#define SAMA_CAN_CAN1_IRQ_PRIORITY          6

/*
 * CLASSD driver system settings.
 */
#define SAMA_USE_CLASSD                     FALSE
#define SAMA_CLASSD_DMA_IRQ_PRIORITY        4
#define SAMA_CLASSD_DMA_ERROR_HOOK(classdp) osalSysHalt("DMA failure")

/*
 * CRY driver system settings.
 */
#define PLATFORM_CRY_USE_CRY1               FALSE

/*
 * I2C driver system settings.
 */
#define SAMA_I2C_USE_TWIHS0                 FALSE
#define SAMA_I2C_USE_TWIHS1                 FALSE
#define SAMA_I2C_USE_FLEXCOM0               FALSE
#define SAMA_I2C_USE_FLEXCOM1               FALSE
#define SAMA_I2C_USE_FLEXCOM2               FALSE
#define SAMA_I2C_USE_FLEXCOM3               FALSE
#define SAMA_I2C_USE_FLEXCOM4               FALSE
#define SAMA_I2C_BUSY_TIMEOUT               50
#define SAMA_I2C_TWIHS0_IRQ_PRIORITY        6
#define SAMA_I2C_TWIHS1_IRQ_PRIORITY        6
#define SAMA_I2C_FLEXCOM0_IRQ_PRIORITY      6
#define SAMA_I2C_FLEXCOM1_IRQ_PRIORITY      6
#define SAMA_I2C_FLEXCOM2_IRQ_PRIORITY      6
#define SAMA_I2C_FLEXCOM3_IRQ_PRIORITY      6
#define SAMA_I2C_FLEXCOM4_IRQ_PRIORITY      6
#define SAMA_I2C_TWIHS0_DMA_IRQ_PRIORITY    6
#define SAMA_I2C_TWIHS1_DMA_IRQ_PRIORITY    6
#define SAMA_I2C_FLEXCOM0_DMA_IRQ_PRIORITY  6
#define SAMA_I2C_FLEXCOM1_DMA_IRQ_PRIORITY  6
#define SAMA_I2C_FLEXCOM2_DMA_IRQ_PRIORITY  6
#define SAMA_I2C_FLEXCOM3_DMA_IRQ_PRIORITY  6
#define SAMA_I2C_FLEXCOM4_DMA_IRQ_PRIORITY  6
#define SAMA_I2C_DMA_ERROR_HOOK(i2cp)       osalSysHalt("DMA failure")

/*
 * ICU driver system settings.
 */
#define SAMA_ICU_USE_TC0                    FALSE
#define SAMA_ICU_USE_TC1                    FALSE
#define SAMA_ICU_TC0_IRQ_PRIORITY           3
#define SAMA_ICU_TC1_IRQ_PRIORITY           3

/*
 * L2CC related defines.
 */
#define SAMA_L2CC_ASSUME_ENABLED            0
#define SAMA_L2CC_ENABLE                    1

/*
 * ONEWIRE driver system settings.
 */
#define SAMA_USE_ONEWIRE                    FALSE

/*
 * LCDC driver system settings.
 */
#define SAMA_USE_LCDC                       FALSE

/*
 * QSPI driver system settings.
 */
#define SAMA_QSPI_USE_QUADSPI0              FALSE
#define SAMA_QSPI_USE_QUADSPI1              FALSE
#define SAMA_QSPI_QUADSPI0_IRQ_PRIORITY     7
#define SAMA_QSPI_QUADSPI1_IRQ_PRIORITY     7
#define SAMA_QSPI_QUADSPI0_DMA_IRQ_PRIORITY 7
#define SAMA_QSPI_QUADSPI1_DMA_IRQ_PRIORITY 7
#define SAMA_QSPI_DMA_ERROR_HOOK(qspip)     osalSysHalt("DMA failure")
#define SAMA_QSPI_CACHE_USER_MANAGED        FALSE
#define SAMA_QSPI_IS_SCRAMBLED              FALSE

/*
 * SDMMC driver system settings.
 */
#define SAMA_USE_SDMMC                      FALSE
#define PLATFORM_SDMMC_USE_SDMMC1           FALSE

/*
 * SECUMOD driver system settings.
 */
#define SAMA_USE_SECUMOD                    FALSE
#define SAMA_SECUMOD_IRQ_PRIORITY           7
#define SAMA_SECURAM_IRQ_PRIORITY           7
#define SAMA_SECUMOD_ENABLE_NOPA            FALSE

/*
 * SERIAL driver system settings.
 */
#define SAMA_SERIAL_USE_UART0               FALSE
#define SAMA_SERIAL_USE_UART1               TRUE    /* GTV Debug */
#define SAMA_SERIAL_USE_UART2               FALSE
#define SAMA_SERIAL_USE_UART3               FALSE
#define SAMA_SERIAL_USE_UART4               FALSE
#define SAMA_SERIAL_USE_FLEXCOM0            FALSE
#define SAMA_SERIAL_USE_FLEXCOM1            FALSE
#define SAMA_SERIAL_USE_FLEXCOM2            FALSE
#define SAMA_SERIAL_USE_FLEXCOM3            FALSE
#define SAMA_SERIAL_USE_FLEXCOM4            FALSE
#define SAMA_SERIAL_UART0_IRQ_PRIORITY      4
#define SAMA_SERIAL_UART1_IRQ_PRIORITY      4
#define SAMA_SERIAL_UART2_IRQ_PRIORITY      4
#define SAMA_SERIAL_UART3_IRQ_PRIORITY      4
#define SAMA_SERIAL_UART4_IRQ_PRIORITY      4
#define SAMA_SERIAL_FLEXCOM0_IRQ_PRIORITY   4
#define SAMA_SERIAL_FLEXCOM1_IRQ_PRIORITY   4
#define SAMA_SERIAL_FLEXCOM2_IRQ_PRIORITY   4
#define SAMA_SERIAL_FLEXCOM3_IRQ_PRIORITY   4
#define SAMA_SERIAL_FLEXCOM4_IRQ_PRIORITY   4

/*
 * SPI driver system settings.
 */
#define SAMA_SPI_USE_SPI0                   FALSE
#define SAMA_SPI0_USE_GCLK                  FALSE
#define SAMA_SPI0_GCLK_SOURCE               SAMA_GCLK_MCK_CLK
#define SAMA_SPI0_GCLK_DIV                  21
#define SAMA_SPI_SPI0_DMA_IRQ_PRIORITY      4

#define SAMA_SPI_USE_SPI1                   FALSE
#define SAMA_SPI1_USE_GCLK                  FALSE
#define SAMA_SPI1_GCLK_SOURCE               SAMA_GCLK_MCK_CLK
#define SAMA_SPI1_GCLK_DIV                  21
#define SAMA_SPI_SPI1_DMA_IRQ_PRIORITY      4

#define SAMA_SPI_USE_FLEXCOM0               FALSE
#define SAMA_FSPI0_USE_GCLK                 FALSE
#define SAMA_FSPI0_GCLK_SOURCE              SAMA_GCLK_MCK_CLK
#define SAMA_FSPI0_GCLK_DIV                 21
#define SAMA_SPI_FLEXCOM0_DMA_IRQ_PRIORITY  4

#define SAMA_SPI_USE_FLEXCOM1               FALSE
#define SAMA_FSPI1_USE_GCLK                 FALSE
#define SAMA_FSPI1_GCLK_SOURCE              SAMA_GCLK_MCK_CLK
#define SAMA_FSPI1_GCLK_DIV                 21
#define SAMA_SPI_FLEXCOM1_DMA_IRQ_PRIORITY  4

#define SAMA_SPI_USE_FLEXCOM2               FALSE
#define SAMA_FSPI2_USE_GCLK                 FALSE
#define SAMA_FSPI2_GCLK_SOURCE              SAMA_GCLK_MCK_CLK
#define SAMA_FSPI2_GCLK_DIV                 21
#define SAMA_SPI_FLEXCOM2_DMA_IRQ_PRIORITY  4

#define SAMA_SPI_USE_FLEXCOM3               FALSE
#define SAMA_FSPI3_USE_GCLK                 FALSE
#define SAMA_FSPI3_GCLK_SOURCE              SAMA_GCLK_MCK_CLK
#define SAMA_FSPI3_GCLK_DIV                 21
#define SAMA_SPI_FLEXCOM3_DMA_IRQ_PRIORITY  4

#define SAMA_SPI_USE_FLEXCOM4               FALSE
#define SAMA_FSPI4_USE_GCLK                 FALSE
#define SAMA_FSPI4_GCLK_SOURCE              SAMA_GCLK_MCK_CLK
#define SAMA_FSPI4_GCLK_DIV                 21
#define SAMA_SPI_FLEXCOM4_DMA_IRQ_PRIORITY  4

#define SAMA_SPI_DMA_ERROR_HOOK(spip)       osalSysHalt("DMA failure")
#define SAMA_SPI_CACHE_USER_MANAGED         FALSE

/*
 * ST driver settings.
 */
#define SAMA_ST_USE_PIT                     TRUE
#define SAMA_ST_USE_TC0                     FALSE
#define SAMA_ST_USE_TC1                     FALSE

/*
 * TC driver system settings.
 */
#define SAMA_USE_TC                         FALSE
#define SAMA_USE_TC0                        FALSE
#define SAMA_USE_TC1                        FALSE
#define SAMA_TC0_IRQ_PRIORITY               2
#define SAMA_TC1_IRQ_PRIORITY               2

/*
 * TRNG driver system settings
 */
#define SAMA_TRNG_USE_TRNG0                 FALSE

/*
 * UART driver system settings.
 */
#define SAMA_UART_USE_UART0                 FALSE
#define SAMA_UART_USE_UART1                 FALSE
#define SAMA_UART_USE_UART2                 TRUE
#define SAMA_UART_USE_UART3                 FALSE
#define SAMA_UART_USE_UART4                 FALSE
#define SAMA_UART_USE_FLEXCOM0              FALSE
#define SAMA_UART_USE_FLEXCOM1              TRUE
#define SAMA_UART_USE_FLEXCOM2              FALSE
#define SAMA_UART_USE_FLEXCOM3              FALSE
#define SAMA_UART_USE_FLEXCOM4              FALSE
#define SAMA_UART_UART0_IRQ_PRIORITY        4
#define SAMA_UART_UART1_IRQ_PRIORITY        4
#define SAMA_UART_UART2_IRQ_PRIORITY        4
#define SAMA_UART_UART3_IRQ_PRIORITY        4
#define SAMA_UART_UART4_IRQ_PRIORITY        4
#define SAMA_UART_FLEXCOM0_IRQ_PRIORITY     4
#define SAMA_UART_FLEXCOM1_IRQ_PRIORITY     4
#define SAMA_UART_FLEXCOM2_IRQ_PRIORITY     4
#define SAMA_UART_FLEXCOM3_IRQ_PRIORITY     4
#define SAMA_UART_FLEXCOM4_IRQ_PRIORITY     4
#define SAMA_UART_UART0_DMA_IRQ_PRIORITY    4
#define SAMA_UART_UART1_DMA_IRQ_PRIORITY    4
#define SAMA_UART_UART2_DMA_IRQ_PRIORITY    4
#define SAMA_UART_UART3_DMA_IRQ_PRIORITY    4
#define SAMA_UART_UART4_DMA_IRQ_PRIORITY    4
#define SAMA_UART_FLEXCOM0_DMA_IRQ_PRIORITY 4
#define SAMA_UART_FLEXCOM1_DMA_IRQ_PRIORITY 4
#define SAMA_UART_FLEXCOM2_DMA_IRQ_PRIORITY 4
#define SAMA_UART_FLEXCOM3_DMA_IRQ_PRIORITY 4
#define SAMA_UART_FLEXCOM4_DMA_IRQ_PRIORITY 4
#define SAMA_UART_DMA_ERROR_HOOK(uartp)     osalSysHalt("DMA failure")
#define SAMA_UART_CACHE_USER_MANAGED        TRUE

/*
 * USB Device driver system settings.
 */
#define SAMA_USB_USE_UDPHS                  FALSE
#define SAMA_USB_UDPHS_IRQ_PRIORITY         6

/*
 * USB Host driver system settings.
 */
#define SAMA_USB_USE_UHPHS                   FALSE
#define SAMA_USB_UHPHS_IRQ_PRIORITY          5
#define USB_HOST_MSD_INSTANCES_NUMBER        1
/* Number of Logical Units */
#define USB_HOST_SCSI_INSTANCES_NUMBER       1
#define USB_HOST_MSD_LUN_NUMBERS             1
/* Maximum USB driver instances */
#define DRV_USB_UHP_INSTANCES_NUMBER         1
/* Interrupt mode enabled */
#define DRV_USB_UHP_INTERRUPT_MODE           TRUE
/* Number of NAKs to wait before returning transfer failure */
#define DRV_USB_UHP_NAK_LIMIT                2000
/* Maximum Number of pipes */
#define DRV_USB_UHP_PIPES_NUMBER             10
/* Attach Debounce duration in milliseconds */
#define DRV_USB_UHP_ATTACH_DEBOUNCE_DURATION 500
/* Reset duration in milli Seconds */
#define DRV_USB_UHP_RESET_DURATION           100
/* Maximum Transfer Size */
#define DRV_USB_UHP_NO_CACHE_BUFFER_LENGTH   4096
/* Number of Endpoints used */
#define DRV_USB_UHP_ENDPOINTS_NUMBER         1
/* Total number of devices to be supported */
#define USB_HOST_DEVICES_NUMBER              1
/* Size of Endpoint 0 buffer */
#define USB_DEVICE_EP0_BUFFER_SIZE           64
/* Target peripheral list entries */
#define  USB_HOST_TPL_ENTRIES                1
/* Maximum number of configurations supported per device */
#define USB_HOST_DEVICE_INTERFACES_NUMBER    5
#define USB_HOST_CONTROLLERS_NUMBER          1
#define USB_HOST_TRANSFERS_NUMBER            10
/* Provides Host pipes number */
#define USB_HOST_PIPES_NUMBER                10
/* Number of Host Layer Clients */
#define USB_HOST_CLIENTS_NUMBER              1

#endif /* MCUCONF_H */
