#ifndef SETTINGS_SERVICE_H
#define SETTINGS_SERVICE_H
#include "app_logic.h"
#include "joy_input.h"
void settings_boot_init(void);
void settings_model_load(app_model_t *m);
int settings_action(app_model_t *m,uint8_t action);
void settings_service_apply(app_model_t *m,int voice_busy);
/* Input-task owner. Returns calibrated screen-relative direction. */
void settings_input_init(joy_input_t *joy);
joy_dir_t settings_input_step(joy_input_t *joy,uint16_t raw,int valid,uint32_t now,unsigned rotation);
#endif
