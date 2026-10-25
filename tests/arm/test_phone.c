#include "common.h"
#include <stdarg.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
static void phone_delay(TickType_t ticks);
#define vTaskDelay phone_delay
#include "../../firmware/app/phone_probe.c"
#undef vTaskDelay
static struct at_client test_client = { .status = AT_STATUS_INITIALIZED };
static struct at_response test_response;
static unsigned dials, hangups, status_queries;
static void phone_delay(TickType_t ticks)
{
    if (ticks == pdMS_TO_TICKS(30000)) {
        CHECK(dials == 1 && hangups == 2 && status_queries == 1);
        test_finish(0);
    }
}
void board_log(const char *fmt, ...) { (void)fmt; }
at_client_t at_client_get_first(void) { return &test_client; }
at_response_t at_create_resp(size_t b, size_t n, int32_t t)
{ (void)b; (void)n; (void)t; memset(&test_response, 0, sizeof test_response); return &test_response; }
void at_delete_resp(at_response_t r) { (void)r; }
const char *at_resp_get_line(at_response_t r, size_t n) { (void)r; (void)n; return NULL; }
int at_obj_set_urc_table(at_client_t c, const struct at_urc *u, size_t n)
{ (void)c; (void)u; (void)n; return QOSA_OK; }
int at_obj_exec_cmd(at_client_t c, at_response_t r, const char *fmt, ...)
{
    (void)c; (void)r;
    if (strncmp(fmt, "ATD", 3) == 0) { ++dials; return QOSA_ERROR_TIMEOUT; }
    va_list ap; va_start(ap, fmt);
    const char *cmd = strcmp(fmt, "%s") == 0 ? va_arg(ap, const char *) : fmt;
    va_end(ap);
    if (strcmp(cmd, "AT+CLCC") == 0 && dials) ++status_queries;
    if (strcmp(cmd, "ATH") == 0) return ++hangups == 1 ? QOSA_ERROR_TIMEOUT : QOSA_OK;
    return QOSA_OK;
}
void test_main(void) { phone_probe_task(NULL); test_finish(999); }
