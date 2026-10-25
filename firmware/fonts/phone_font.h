#ifndef PHONE_FONT_H
#define PHONE_FONT_H
#include <stdint.h>
/* Twelve 12-bit rows, MSB at left; unsupported Unicode returns a boxed glyph. */
const uint16_t *phone_font_rows(uint32_t codepoint);
#endif
