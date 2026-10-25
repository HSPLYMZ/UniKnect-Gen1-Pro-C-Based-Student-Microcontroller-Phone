#ifndef SETTINGS_DATA_H
#define SETTINGS_DATA_H
#include <stdint.h>
typedef struct {
    uint16_t idle,levels[5];
    uint8_t rotation,calibration_rotation,manual;
} settings_config_t;
enum { SETTINGS_SAVED,SETTINGS_PENDING,SETTINGS_WRITING,SETTINGS_SAVE_FAILED,SETTINGS_FULL };
enum { CAL_NONE,CAL_RUNNING,CAL_SUCCESS,CAL_CANCELLED,CAL_TIMEOUT,CAL_INCOMING,CAL_BUSY,CAL_INVALID };
typedef struct {
    settings_config_t desired,saved;
    uint32_t revision,saved_revision,sequence,cal_id,cal_revision;
    uint8_t save_state,cal_result,cal_origin,layout_rotation;
} settings_state_t;
void settings_defaults(settings_config_t *c);
int settings_valid(const settings_config_t *c);
int settings_equal(const settings_config_t *a,const settings_config_t *b);
#endif
