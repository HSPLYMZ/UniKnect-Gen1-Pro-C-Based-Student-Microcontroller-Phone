#ifndef SIGNAL_DATA_H
#define SIGNAL_DATA_H
#include <stdint.h>
#include <stddef.h>
enum { SIGNAL_RSRP=1u, SIGNAL_SINR=2u, SIGNAL_RSRQ=4u, SIGNAL_RSSI=8u,
       SIGNAL_BAND=16u, SIGNAL_CELL=32u, SIGNAL_OPERATOR=64u };
enum { SIGNAL_ERR_CELL=1u, SIGNAL_ERR_OPERATOR=2u };
enum { SIGNAL_IDLE, SIGNAL_QUEUED, SIGNAL_FETCHING, SIGNAL_DEFERRED, SIGNAL_DONE, SIGNAL_FAILED };
typedef struct {
    uint64_t updated_ms;
    uint32_t utc,cell_id;
    int16_t rsrp,rsrq,rssi,sinr_raw;
    uint16_t band,valid;
    char operator_name[64];
    uint8_t errors,registered,lte,present;
} signal_sample_t;
typedef struct {
    signal_sample_t sample;
    uint32_t request_id,delivery_revision,completed_id;
    uint8_t state,refresh_requested,scroll,layout_rotation;
} signal_state_t;
int signal_parse_serving(const char *line,signal_sample_t *sample);
int signal_parse_operator(const char *line,signal_sample_t *sample);
unsigned signal_visible(unsigned rotation);
unsigned signal_line(const signal_sample_t *sample,unsigned units,unsigned index,char *out,size_t capacity);
unsigned signal_lines(const signal_sample_t *sample,unsigned units);
void signal_half_db(char *out,size_t capacity,int raw);
#endif
