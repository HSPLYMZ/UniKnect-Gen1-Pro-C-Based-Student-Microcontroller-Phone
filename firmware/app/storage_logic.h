#ifndef STORAGE_LOGIC_H
#define STORAGE_LOGIC_H
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint64_t uptime_ms;
    uint16_t light_raw,input_raw;
    int16_t x_mg,y_mg,z_mg,rsrp_dbm;
} storage_record_t;

int storage_format_record(char *out,size_t capacity,const storage_record_t *record);

/* Pure, bounded parsers for EC200U FILE responses. QFLDS can exceed 32 bits. */
int storage_parse_space(const char *line, uint64_t *free_bytes, uint64_t *total_bytes);
int storage_parse_handle(const char *line, int *handle);
int storage_parse_position(const char *line, uint32_t *position);
int storage_parse_write(const char *line, uint32_t *written);

#endif
