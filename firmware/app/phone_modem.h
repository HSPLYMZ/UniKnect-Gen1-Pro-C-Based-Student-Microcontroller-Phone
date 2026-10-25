#ifndef PHONE_MODEM_H
#define PHONE_MODEM_H
#include "phone_logic.h"
/* EC200U QuecCell V1.3 serving-cell response -> typed network event.
 * This never treats CSQ (possibly GSM) as LTE RSRP. No I/O or state mutation. */
int phone_modem_qeng(const char *line, phone_event_t *event);
/* AT+QLTS=2 local network time, or explicit CCLK RTC response.
 * QLTS=0/1 must not be passed here; both can carry a misleading local suffix. */
int phone_modem_clock(const char *line,phone_event_t *event);
#endif
