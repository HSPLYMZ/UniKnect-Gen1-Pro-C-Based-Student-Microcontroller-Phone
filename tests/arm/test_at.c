/* Real AT C code + newlib formatting, with deterministic UART/OS fault inputs. */
#include "common.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "at.h"
#include "qosa_log.h"
static int fail_alloc, fail_format;
static void *fault_calloc(size_t n, size_t size)
{ if (fail_alloc && --fail_alloc == 0) return NULL; return calloc(n, size); }
static void *fault_realloc(void *p, size_t n)
{ if (fail_alloc && --fail_alloc == 0) return NULL; return realloc(p, n); }
static int fault_vsnprintf(char *b, size_t n, const char *f, va_list ap)
{ return fail_format ? -1 : vsnprintf(b, n, f, ap); }
#define calloc fault_calloc
#define realloc fault_realloc
#include "../../firmware/third_party/at_client/src/at_client.c"
#undef calloc
#undef realloc
#define vsnprintf fault_vsnprintf
#include "../../firmware/third_party/at_client/src/at_utils.c"
#undef vsnprintf

LogLevel g_debug_level = LOG_ERROR;
void qosa_log_write(int l, const char *t, const char *f, const char *fmt, ...)
{ (void)l; (void)t; (void)f; (void)fmt; }
void qosa_assert_failed(const char *e, const char *f, int n) { (void)e; (void)f; test_finish(n + 10000); }
typedef struct { unsigned count, depth; } fake_handle;
static fake_handle handles[32];
static unsigned handle_count;
static at_client_t cli;
static const char *input;
static size_t input_left;
static char transmitted[4096];
static size_t transmitted_size, payload_size;
static int mode, data_reply, short_write, callback_done;
enum { RESP_OK, RESP_ERROR, RESP_TIMEOUT, RESP_PROMPT, RESP_CALLBACK };

static void line(const char *s)
{
    size_t n = strlen(s);
    CHECK(n < cli->recv_bufsz);
    memcpy(cli->recv_line_buf, s, n + 1);
    cli->recv_line_len = n;
    cli->urc = NULL;
    client_process_line(cli);
}
int qosa_mutex_create(osa_mutex_t *h) { CHECK(handle_count < 32); *h = &handles[handle_count++]; return QOSA_OK; }
int qosa_mutex_lock(osa_mutex_t h, u32_t t) { (void)t; CHECK(h); ++((fake_handle *)h)->depth; return QOSA_OK; }
int qosa_mutex_unlock(osa_mutex_t h) { CHECK(h && ((fake_handle *)h)->depth); --((fake_handle *)h)->depth; return QOSA_OK; }
int qosa_mutex_delete(osa_mutex_t h) { (void)h; return QOSA_OK; }
int qosa_sem_create(osa_sem_t *h, u32_t n) { qosa_mutex_create(h); ((fake_handle *)*h)->count = n; return QOSA_OK; }
int qosa_sem_delete(osa_sem_t h) { (void)h; return QOSA_OK; }
int qosa_sem_release(osa_sem_t h)
{
    if (mode == RESP_CALLBACK && h == cli->resp_notice) CHECK(callback_done);
    ++((fake_handle *)h)->count; return QOSA_OK;
}
int qosa_sem_wait(osa_sem_t h, u32_t t)
{
    fake_handle *s = h;
    if (!s->count && t && h == cli->resp_notice && mode != RESP_TIMEOUT)
        line(mode == RESP_ERROR ? "ERROR\r\n" : mode == RESP_PROMPT ? ">" : "OK\r\n");
    if (!s->count) return QOSA_ERROR_TIMEOUT;
    --s->count; return QOSA_OK;
}
int qosa_task_create(osa_task_t *h, u32_t n, uint8_t p, char *name, void (*fn)(void *), void *a, ...)
{ (void)n; (void)p; (void)name; (void)fn; (void)a; *h = (void *)2; return QOSA_OK; }
osa_task_t qosa_task_get_current_ref(void) { return (void *)1; }
void qosa_task_sleep_ms(u32_t n) { (void)n; }
int qosa_uart_open(void) { return QOSA_OK; }
void qosa_uart_register(event_callback_t fn) { (void)fn; }
int qosa_uart_read(int pos, const char *buf, size_t size)
{
    (void)pos;
    size_t n = MIN(size, input_left);
    memcpy((void *)buf, input, n); input += n; input_left -= n; return (int)n;
}
int qosa_uart_write(int pos, const char *buf, size_t n)
{
    (void)pos;
    CHECK(transmitted_size + n <= sizeof transmitted);
    memcpy(transmitted + transmitted_size, buf, n); transmitted_size += n;
    if (n && buf[0] == 'P') {
        payload_size += n;
        if (data_reply >= 0) line(data_reply ? "SEND FAIL\r\n" : "SEND OK\r\n");
    }
    return (int)(short_write && n ? n - 1 : n);
}
static size_t format_line(char *b, size_t n, const char *fmt, ...)
{ va_list ap; va_start(ap, fmt); size_t sent = at_vprintfln(b, n, fmt, ap); va_end(ap); return sent; }
static void callback(const char *s, size_t n, void *arg)
{
    (void)s; (void)n;
    CHECK(arg == &callback_done);
    CHECK(((fake_handle *)cli->resp_lock)->depth > 0);
    CHECK(((fake_handle *)cli->resp_notice)->count == 0);
    callback_done = 1;
}
static void urc_callback(struct at_client *c, const char *s, size_t n, void *a)
{ (void)c; (void)s; (void)n; (void)a; }

void test_main(void)
{
    cli = at_client_get_first();
    CHECK(at_client_init(cli, 1024, 256) == QOSA_OK);
    CHECK(cli->resp_lock != NULL);
    char large[301]; memset(large, 'X', 300); large[300] = 0;
    struct { uint8_t before[8]; char buf[256]; uint8_t after[8]; } guarded;
    memset(&guarded, 0xA5, sizeof guarded);
    for (unsigned n = 253; n <= 300; ++n) {
        large[n] = 0;
        size_t before = transmitted_size;
        size_t sent = format_line(guarded.buf, sizeof guarded.buf, "%s", large);
        CHECK(n <= 254 ? sent == n + 1 : sent == 0);
        CHECK(n <= 254 || transmitted_size == before);
        for (unsigned i = 0; i < 8; ++i) CHECK(guarded.before[i] == 0xA5 && guarded.after[i] == 0xA5);
        large[n] = 'X';
    }
    large[300] = 0;
    fail_format = 1; CHECK(format_line(guarded.buf, 256, "AT") == 0); fail_format = 0;
    CHECK(format_line(guarded.buf, 1, "AT") == 0);
    transmitted_size = 0;
    CHECK(at_obj_exec_cmd(cli, NULL, "AT") == QOSA_OK);
    CHECK(transmitted_size == 3 && memcmp(transmitted, "AT\r", 3) == 0);
    at_response_t r = at_create_resp(32, 0, 20); CHECK(r);
    CHECK(at_obj_exec_cmd(cli, r, "%s", large) == QOSA_ERROR_PARAM_INVALID);
    CHECK(cli->resp == NULL);
    mode = RESP_ERROR; CHECK(at_obj_exec_cmd(cli, r, "AT") == QOSA_ERROR_GENERAL);
    mode = RESP_TIMEOUT; CHECK(at_obj_exec_cmd(cli, r, "AT") == QOSA_ERROR_TIMEOUT);
    CHECK(cli->resp == NULL && cli->arg == NULL && cli->self_func == NULL);
    at_delete_resp(r);
    line("LATE REPLY\r\n"); /* detached/freed response cannot be accessed */
    mode = RESP_OK;
    fail_alloc = 1; CHECK(at_create_resp_by_selffunc(8, 0, 20, callback) == NULL);
    fail_alloc = 1; CHECK(at_create_resp_by_selffunc_new(8, 0, 20, callback, NULL) == NULL);
    r = at_create_resp(8, 0, 20); CHECK(r); char *old = r->buf;
    fail_alloc = 1; CHECK(at_resp_set_info(r, 1000, 0, 20) == NULL);
    CHECK(r->buf == old && r->buf_size == 8);
    fail_alloc = 1; CHECK(at_resp_set_info_new(r, 1000, 0, 20, NULL) == NULL);
    CHECK(r->buf == old && r->buf_size == 8); at_delete_resp(r);
    const struct at_urc table[] = {{"+TEST", "\r\n", urc_callback}};
    CHECK(at_obj_set_urc_table(cli, table, 1) == QOSA_OK);
    struct at_urc_table *old_table = cli->urc_table;
    fail_alloc = 1; CHECK(at_obj_set_urc_table(cli, table, 1) == QOSA_ERROR_NO_MEMORY);
    CHECK(cli->urc_table == old_table && cli->urc_table_size == 1);
    r = at_create_resp_new(32, 0, 20, &callback_done); CHECK(r); r->self_func = callback;
    mode = RESP_CALLBACK; CHECK(at_obj_exec_cmd(cli, r, "AT") == QOSA_OK);
    CHECK(callback_done && ((fake_handle *)cli->resp_lock)->depth == 0); at_delete_resp(r);
    mode = RESP_OK;
    char full[1030]; memset(full, 'Z', sizeof full); full[1022] = '\r'; full[1023] = '\n';
    input = full; input_left = 1024;
    CHECK(at_recv_readline(cli) < 0); CHECK(cli->recv_line_len == 0);
    input = "OK\r\n"; input_left = 4;
    CHECK(at_recv_readline(cli) == 4); CHECK(cli->recv_line_buf[4] == 0);
    full[1021] = '\r'; full[1022] = '\n'; input = full; input_left = 1023;
    CHECK(at_recv_readline(cli) == 1023); CHECK(cli->recv_line_buf[1023] == 0);
    mode = RESP_ERROR; transmitted_size = payload_size = 0;
    CHECK(at_obj_exec_cmd_with_data(cli, "AT+SEND=%n", "PAYLOAD", 7) == 0);
    CHECK(transmitted_size == 0);
    CHECK(at_obj_exec_cmd_with_data(cli, "AT+SEND=%u", "PAYLOAD", 7) == 0);
    CHECK(payload_size == 0 && cli->end_sign == 0);
    mode = RESP_PROMPT; data_reply = 0; transmitted_size = payload_size = 0;
    CHECK(at_obj_exec_cmd_with_data(cli, "AT+SEND=%u", "PAYLOAD", 7) == 7);
    CHECK(payload_size == 7 && cli->end_sign == 0);
    data_reply = 1;
    CHECK(at_obj_exec_cmd_with_data(cli, "AT+SEND=%u", "PAYLOAD", 7) == 0);
    data_reply = -1;
    CHECK(at_obj_exec_cmd_with_data(cli, "AT+SEND=%u", "PAYLOAD", 7) == 0);
    short_write = 1;
    CHECK(at_obj_exec_cmd_with_data(cli, "AT+SEND=%u", "PAYLOAD", 7) == 0);
    CHECK(cli->end_sign == 0 && ((fake_handle *)cli->lock)->depth == 0);
    test_finish(0);
}
