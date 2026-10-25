# Hardware / 接线

Target: NUCLEO-F413ZH (STM32F413ZH, 100 MHz, 1.5 MiB flash, 320 KiB SRAM),
QADP-EC200U and the existing 160×128 ST7735S shield.

| Function | Existing connection |
|---|---|
| LCD SPI1 SCK / MOSI | PA5 / PA7 |
| LCD CS / DC | PD14 / PF12 |
| Analog joystick | PC1, ADC1_IN11 |
| Console USART3 TX / RX | PD8 / PD9, 115200 8N1 |
| Modem USART6 RX / TX | PG9 / PG14, 115200 8N1 |
| Modem reset | PD15; owned by the board layer, never by the LCD |

LCD uses software reset, SPI Mode 0, RGB565 and DMA2 Stream3 Channel3 at 6.25 MHz.
Do not repurpose PD15 for the screen. Keep the board's actual wiring and power
arrangement; active PWRKEY/power-control sequencing is not added by this project.
SD logging and motion-based automatic rotation are disabled in the normal profile.

The board does not provide downstream audio playback. Previous manual voice
checks verified the remote handset could hear the board's MIC1.
