#ifndef VOICE_SERVICE_H
#define VOICE_SERVICE_H
#include "phone_logic.h"
#include "voice_logic.h"
typedef struct {
    uint32_t revision, generation, result_id;
    voice_snapshot_t call;
    uint8_t call_valid, note;
} voice_delivery_t;
void voice_service_init(void);
/* Nonzero request ID on success. One outstanding request/result at a time. */
uint32_t voice_service_submit(const phone_intent_t *intent);
int voice_service_receive(uint32_t revision, voice_delivery_t *delivery);
void voice_service_ack(uint32_t request_id);
void voice_service_apply(phone_state_t *phone,uint8_t *page,unsigned rotation,
                         uint64_t now_ms,uint32_t *revision);
int voice_service_busy(void);
void app_voice_task(void *arg);
#endif
