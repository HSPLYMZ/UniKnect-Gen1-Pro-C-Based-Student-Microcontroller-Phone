/* Application parameters for the STM32 phone. Private contact configuration
 * belongs only in phone_number.local.h; see phone_number.example.h. */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#define APP_DEVICE_ID          "cabinet-01"
#define APP_SCHEMA             1
#define APP_FW_VERSION         "1.2.1-privacy"
#ifndef APP_PHONE_ONLY
#define APP_PHONE_ONLY          1
#endif
/* Stable board profile: 100 MHz core, SPI1 /16 = 6.25 MHz. */
#define CFG_CPU_MHZ             100u

/* The only one-touch contact. Calling still requires a physical key action. */
/* Private number: copy phone_number.example.h to phone_number.local.h.
 * Build/test -D overrides intentionally bypass the private file. */
#ifndef CFG_GUARDIAN_PHONE_NUMBER
#if __has_include("phone_number.local.h")
#include "phone_number.local.h"
#endif
#endif
#ifndef CFG_GUARDIAN_PHONE_NUMBER
#define CFG_GUARDIAN_PHONE_NUMBER ""
#endif
/* Normal firmware enables the existing AT link. No call is placed at boot. */
#ifndef APP_MODEM_ENABLE
#define APP_MODEM_ENABLE        1
#endif
#ifndef APP_LCD_DIAGNOSTIC
#define APP_LCD_DIAGNOSTIC      0
#endif
#define CFG_DISPLAY_CELL_BUDGET 520u
/* SPI1 + DMA /16 (6.25 MHz), already visually verified on this board. */
#define CFG_LCD_DEFAULT_PROFILE 14u
#define CFG_MOTION_POLL_MS      50u
#define CFG_MOTION_STABLE_MS    450u
/* Sensor-to-screen mounting transform from 2026-10-24 board observations:
 * upright +X needs 90 degrees; sideward +Y needs 0, -Y needs 180.
 * Screen +X is right, +Y is down in the original 160x128 orientation.
 * Axis indices 0=X, 1=Y, 2=Z; signs must be +1 or -1. */
#define CFG_MOTION_X_AXIS       0
#define CFG_MOTION_Y_AXIS       1
#define CFG_MOTION_Z_AXIS       2
#define CFG_MOTION_X_SIGN       -1
#define CFG_MOTION_Y_SIGN       1
#define CFG_MOTION_ROTATION_OFFSET 0u

/* Periods (ms) */
#define CFG_SAMPLE_MS          1000
#define CFG_LIGHT_POLL_MS      40
#define CFG_HEARTBEAT_MS       60000
#define CFG_DISPLAY_MS         22u /* 45.45 Hz cadence, margin for a 40 FPS floor */
#define CFG_UI_EASE_MS         96u
#define CFG_UI_SCROLL_EASE_MS  72u
#define CFG_SD_LOG_MS          10000
#define CFG_SD_MIN_FREE_BYTES  (2u * 1024u * 1024u)
#define CFG_SD_MAX_FILE_BYTES  (8u * 1024u * 1024u)
#define CFG_SD_RETRY_MS        30000u
#define CFG_GNSS_POLL_MS       5000
#define CFG_LBS_POLL_MS        300000
#define CFG_MQTT_RETRY_MS      5000
#define CFG_MQTT_RETRY_MAX_MS  60000
#define CFG_QUEUE_LIMIT        24

/* Event thresholds */
#define CFG_TEMP_HIGH_C        350   /* x0.1 degC */
#define CFG_TEMP_CLEAR_C       330
#define CFG_HUMIDITY_HIGH_PCT  800   /* x0.1 %RH */
#define CFG_HUMIDITY_CLEAR_PCT 750
#define CFG_LIGHT_EXPOSED_TRIGGER  18000u
#define CFG_LIGHT_EXPOSED_CLEAR    24000u
/* NOTE: V2 read the LDR through machine.ADC.read_u16() (0..65535). The C driver
 * returns the raw 12-bit value (0..4095), and measurement shows v12 * 16 is the
 * same scale V2 saw (1670 * 16 = 26,720 vs V2's 26,600 covered).
 * Compare against these constants only after << 4, or divide them by 16. */
#define CFG_TILT_TRIGGER_DEG   30
#define CFG_TILT_CLEAR_DEG     20
#define CFG_EVENT_CONSECUTIVE  3
#define CFG_EVENT_COOLDOWN_MS  60000

/* Button (shield joystick ladder) */
#define CFG_BUTTON_DELTA       3000
#define CFG_BUTTON_LONG_MS     1000
#define CFG_BUTTON_DEBOUNCE_MS 50
#define CFG_JOY_SCAN_MS        5u
#define CFG_JOY_STABLE_MS      20u
#define CFG_JOY_RELEASE_MS     20u

/* Bring-up probes. Voice dialling is off by default: it rings a real handset,
 * so it must never fire on an ordinary reset. */
#ifndef PHONE_PROBE_ENABLE
#define PHONE_PROBE_ENABLE     0
#endif
#if PHONE_PROBE_ENABLE && !APP_MODEM_ENABLE
#error "PHONE_PROBE_ENABLE requires APP_MODEM_ENABLE"
#endif

#endif /* APP_CONFIG_H */
