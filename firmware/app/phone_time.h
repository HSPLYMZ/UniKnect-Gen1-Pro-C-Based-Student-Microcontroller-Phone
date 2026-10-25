#ifndef PHONE_TIME_H
#define PHONE_TIME_H
#include <stdint.h>
typedef struct { uint16_t year; uint8_t month, day, hour, minute, second; } phone_datetime_t;
typedef struct { uint64_t anchor_ms; uint32_t utc; uint8_t valid, source; } phone_clock_t;
/* Beijing fixed UTC+08:00, Gregorian 2000..2099 input. */
int phone_time_from_beijing(const phone_datetime_t *local, uint32_t *utc);
int phone_time_to_beijing(uint32_t utc, phone_datetime_t *local);
int phone_clock_set(phone_clock_t *clock, uint32_t utc, uint64_t now_ms, uint8_t source);
int phone_clock_read(const phone_clock_t *clock, uint64_t now_ms, phone_datetime_t *local);
int phone_time_parse(const char *text, uint32_t *utc); /* YYYY-MM-DD HH:MM[:SS] */
#endif
