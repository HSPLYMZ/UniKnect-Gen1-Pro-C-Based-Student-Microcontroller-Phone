#ifndef VOICE_LOGIC_H
#define VOICE_LOGIC_H
#include "phone_logic.h"
typedef struct { uint32_t id; uint8_t state, incoming, waiting; char number[PHONE_NUMBER_MAX+1]; } voice_snapshot_t;
/* Return 1 for valid voice records (waiting is tagged, never adopted by UI),
 * 0 for data/nonmatching/malformed input. IDs are preserved for reconciliation. */
int voice_parse_clcc(const char *line,voice_snapshot_t *out);
int voice_parse_dsci(const char *line,voice_snapshot_t *out);
int voice_parse_clip(const char *line,char number[PHONE_NUMBER_MAX+1]);
#endif
