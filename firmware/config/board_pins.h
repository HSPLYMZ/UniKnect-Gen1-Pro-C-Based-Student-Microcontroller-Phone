/* Board definitions for NUCLEO-F413ZH + QADP-EC200U (UniKnect Gen1 Pro).
 *
 * Pin values marked VERIFY are taken from the Quectel hardware guide or UM1974
 * and still need a bench check (see docs/引脚实测记录.md). Do not trust them
 * blindly: the previous MicroPython project used different pin names than the
 * official Arduino table.
 */
#ifndef BOARD_PINS_H
#define BOARD_PINS_H

#include <stdint.h>

#include "stm32f4xx.h"

/* ---- Numeric aliases ----------------------------------------------------
 * The M0/M1 build links CMSIS only, not the ST HAL driver, so the HAL's
 * GPIO_AFx_yyy / ADC_CHANNEL_n macros are not available.
 *
 * Pin macros below are **pin numbers (0..15)**, not masks, because every use
 * is register math: MODER uses `2*n`, AFR uses `4*(n-8)`, ADC uses the channel
 * index. Use BOARD_PIN(n) when a mask is needed (BSRR / ODR).
 */
#define BOARD_PIN(n)     (1u << (n))
#define BOARD_PIN_MASK   BOARD_PIN

#define GPIO_AF4_I2C1   4u
#define GPIO_AF7_USART3 7u
#define GPIO_AF8_USART6 8u

#define ADC_CHANNEL_0   0u
#define ADC_CHANNEL_1   1u
#define ADC_CHANNEL_2   2u
#define ADC_CHANNEL_3   3u
#define ADC_CHANNEL_4   4u
#define ADC_CHANNEL_5   5u
#define ADC_CHANNEL_6   6u
#define ADC_CHANNEL_7   7u
#define ADC_CHANNEL_8   8u
#define ADC_CHANNEL_9   9u
#define ADC_CHANNEL_10  10u
#define ADC_CHANNEL_11  11u
#define ADC_CHANNEL_12  12u
#define ADC_CHANNEL_13  13u
#define ADC_CHANNEL_14  14u
#define ADC_CHANNEL_15  15u
#define ADC_CHANNEL_16  16u
#define ADC_CHANNEL_17  17u
#define ADC_CHANNEL_18  18u

/* ---- On-board user LEDs (UM1974) ---------------------------------------- */
#define LED_GREEN_PORT   GPIOB
#define LED_GREEN_PIN    0    /* LD1 */
#define LED_BLUE_PORT    GPIOB
#define LED_BLUE_PIN     7    /* LD2 */
#define LED_RED_PORT     GPIOB
#define LED_RED_PIN      14   /* LD3 */

/* ---- Console: ST-LINK Virtual COM = USART3 PD8/PD9 ---------------------- */
#define CONSOLE_UART          USART3
#define CONSOLE_UART_IRQn     USART3_IRQn
#define CONSOLE_TX_PORT       GPIOD
#define CONSOLE_TX_PIN        8
#define CONSOLE_RX_PORT       GPIOD
#define CONSOLE_RX_PIN        9
#define CONSOLE_GPIO_AF       GPIO_AF7_USART3
#define CONSOLE_BAUDRATE      115200u

/* ---- EC200U main UART (Arduino D0/D1 = USART6 PG9/PG14) ---------------- */
/* VERIFY: J308 must bridge 1-3 / 2-4 for Nucleo-144 (TXD1/RXD1). */
#define MODEM_UART            USART6
#define MODEM_UART_IRQn       USART6_IRQn
#define MODEM_TX_PORT         GPIOG
#define MODEM_TX_PIN          14
#define MODEM_RX_PORT         GPIOG
#define MODEM_RX_PIN          9
#define MODEM_GPIO_AF         GPIO_AF8_USART6
#define MODEM_BAUDRATE        115200u

/* ---- EC200U control lines ---------------------------------------------- */
/* PWR_EN/PWRKEY still need a board check. RESET was verified 2026-10-22 against
 * the QADP V1.1 schematic and by releasing PD15: USB and AT recovered.
 * The older guide's D2/PF15 reset mapping does not match this board revision. */
#define MODEM_PWR_EN_PORT     GPIOF      /* J301-5, D4: HIGH = module off */
#define MODEM_PWR_EN_PIN      14
#define MODEM_PWRKEY_PORT     GPIOE      /* J301-7, D6: HIGH >= 2 s = power on */
#define MODEM_PWRKEY_PIN      9
#define MODEM_RESET_PORT      GPIOD      /* D9 -> ARD_RST_MOD -> Q206: HIGH = reset */
#define MODEM_RESET_PIN       15

/* ---- Sensors: I2C1 on Arduino D15/D14 ---------------------------------- */
#define SENSOR_I2C            I2C1
#define SENSOR_SCL_PORT       GPIOB
#define SENSOR_SCL_PIN        8
#define SENSOR_SDA_PORT       GPIOB
#define SENSOR_SDA_PIN        9
#define SENSOR_GPIO_AF        GPIO_AF4_I2C1
#define MOTION_I2C_ADDRESS     0x19u /* LIS2DH12TR, previously scanned on this board */

/* ---- LCD: ST7735 on SPI1 ----------------------------------------------- */
#define LCD_SPI               SPI1
#define LCD_SCK_PORT          GPIOA
#define LCD_SCK_PIN           5   /* D13 */
#define LCD_MISO_PORT         GPIOA
#define LCD_MISO_PIN          6   /* D12 */
#define LCD_MOSI_PORT         GPIOA
#define LCD_MOSI_PIN          7   /* D11 */
#define LCD_CS_PORT           GPIOD
#define LCD_CS_PIN            14  /* D10 */
#define LCD_DC_PORT           GPIOF
#define LCD_DC_PIN            12  /* D8 */
/* LCD reset follows the Arduino RESET net; use SWRESET in the display driver.
 * D9/PD15 belongs to MODEM_RESET and must never be driven as LCD reset. */

/* ---- Analog inputs -----------------------------------------------------
 * VERIFIED on hardware 2026-10-21 (docs/引脚实测记录.md).
 *
 * The hardware guide claims the LDR sits on Arduino A0 (= PA3 per UM1974),
 * but PA3 measures a flat ~4030 and does not react to covering the sensor.
 * The TFT shield occupies that net. The V2 application reads PC5 and PC5 is
 * the only pin that responds to light, so PC5 is the LDR.
 *
 * Measured in V2's 16-bit scale: exposed ~28700, covered ~26600. C raw is 12-bit
 * (roughly 1794/1663); convert with << 4 before using V2 thresholds. The documented
 * 13000/38000 swing still needs a flashlight test before thresholds are fixed.
 *
 * ADC rules for the driver: one channel at a time, sample time >= 480 cycles,
 * discard the first conversion after a channel switch. Rapid multi-channel
 * reads bleed into each other (PA0 read 20565 first, then settled at 7601).
 */
#define LDR_ADC               ADC1
#define LDR_ADC_CHANNEL       ADC_CHANNEL_15  /* PC5, verified */
#define LDR_ADC_PORT          GPIOC
#define LDR_ADC_PIN           5

#define BUTTON_ADC            ADC1
#define BUTTON_ADC_CHANNEL    ADC_CHANNEL_11  /* PC1, verified idle ~65170 */
#define BUTTON_ADC_PORT       GPIOC
#define BUTTON_ADC_PIN        1

#endif /* BOARD_PINS_H */
