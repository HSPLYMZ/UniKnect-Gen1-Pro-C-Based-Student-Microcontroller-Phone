#ifndef SIGNAL_SERVICE_H
#define SIGNAL_SERVICE_H
#include "phone_logic.h"
void signal_service_init(void);
void signal_service_set_waker(void (*wake)(void));
uint32_t signal_service_request(void);
int signal_service_pending(void);
void signal_service_worker(void *at_client);
void signal_service_apply(phone_state_t *phone,uint64_t now_ms,unsigned rotation);
#endif
