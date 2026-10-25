#include "at_probe.h"

#include "FreeRTOS.h"
#include "task.h"

#include "at.h"
#include "board.h"
#include "qosa_log.h"
#include "uart_at.h"
#include "app_runtime.h"
#include "phone_modem.h"
#include "voice_service.h"
#include "signal_service.h"
static TaskHandle_t s_probe_task;
static void signal_wake_probe(void) {if(s_probe_task)xTaskNotifyGive(s_probe_task);}

#define PROBE_TASK_STACK_WORDS  1024u
#define PROBE_TASK_PRIO         4u

#define PROBE_RESP_SIZE         512u
#define PROBE_CMD_TIMEOUT_MS    2000

typedef struct {
    const char *cmd;
    const char *why;
    int         timeout_ms;
} probe_step_t;

/* Ordered cheapest-first: if "AT" fails nothing else is worth trying. */
static const probe_step_t s_steps[] = {
    {"ATE0",  "echo off", 1000},
    {"AT",    "link alive", 1000},
    {"ATI",   "module model/firmware", 2000},
    {"AT+CPIN?", "SIM state", 2000},
    {"AT+CSQ", "signal quality", 2000},
    {"AT+CREG?", "2G registration", 2000},
    {"AT+CEREG?", "LTE registration", 2000},
    {"AT+QENG=\"servingcell\"", "serving cell (RSRP etc.)", 3000},
    {"AT+QLTS=2", "current local network time", 2000},
};

/* Runs one command and prints its response lines. Returns 0 on OK. */
static int probe_cmd(at_client_t cli, const probe_step_t *step)
{
    at_response_t resp = at_create_resp(PROBE_RESP_SIZE, 0, step->timeout_ms);
    if (resp == NULL) {
        board_log("[probe] %-8s no memory for response object", step->cmd);
        return -1;
    }

    int rc = at_obj_exec_cmd(cli, resp, "%s", step->cmd);

    if (rc == QOSA_OK) {
        board_log("[probe] %-22s OK   (%s)", step->cmd, step->why);
        for (size_t i = 1; i <= resp->line_counts; ++i) {
            const char *line = at_resp_get_line(resp, i);
            if (line != NULL && line[0] != '\0') {
                board_log("           | %s", line);
                app_event_t event={.kind=APP_EVENT_PHONE};
                if(phone_modem_qeng(line,&event.value.phone) || phone_modem_clock(line,&event.value.phone))
                    (void)app_event_post(&event);
            }
        }
    } else {
        board_log("[probe] %-22s FAIL rc=%d (%s)", step->cmd, rc, step->why);
        for (size_t i = 1; i <= resp->line_counts; ++i) {
            const char *line = at_resp_get_line(resp, i);
            if (line != NULL && line[0] != '\0') {
                board_log("           | %s", line);
            }
        }
    }

    at_delete_resp(resp);
    return rc;
}

static size_t probe_suite(at_client_t cli)
{
    size_t pass = 0;
    size_t total = sizeof s_steps / sizeof s_steps[0];
    for (size_t i = 0; i < total; ++i) {
        if (probe_cmd(cli, &s_steps[i]) == QOSA_OK) {
            pass++;
        }
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    at_uart_stats_t st;
    at_uart_get_stats(&st);
    board_log("[probe] ---- %u/%u passed ----", (unsigned)pass, (unsigned)total);
    board_log("[probe] uart: rx=%lu tx=%lu idle=%lu wrap=%lu ore=%lu drop=%lu",
              (unsigned long)st.rx_bytes, (unsigned long)st.tx_bytes,
              (unsigned long)st.idle_events, (unsigned long)st.dma_wraps,
              (unsigned long)st.overrun_errors, (unsigned long)st.ring_dropped);
    board_log("[probe] dma: errors=%lu late=%lu", (unsigned long)st.dma_errors,
              (unsigned long)st.dma_late_events);

    if (pass == 0) {
        board_log("[probe] nothing answered. Check: J308 bridge, module powered,"
                  " 115200 8N1, PG9=RX / PG14=TX.");
    }

    return pass;
}

static void probe_task(void *arg)
{
    (void)arg;

    at_client_t cli = at_client_get_first();
    signal_service_set_waker(signal_wake_probe);

    board_log("[probe] at_client_init(recv=1024 tx=256) ...");
    int rc = at_client_init(cli, 1024, 256);
    if (rc != QOSA_OK) {
        board_log("[probe] at_client_init FAILED rc=%d -> check USART6 wiring", rc);
        for (;;) {
            signal_service_worker(cli);
            (void)ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(5000));
        }
    }

    vTaskDelay(pdMS_TO_TICKS(200)); /* let the parser task start */

    size_t pass = probe_suite(cli);
    board_log("[probe] continuous AT health check enabled; no power/reset pin changes");

    TickType_t next_background=xTaskGetTickCount()+pdMS_TO_TICKS(pass?30000u:5000u);
    for (;;) {
        signal_service_worker(cli);
        TickType_t now=xTaskGetTickCount();
        if((int32_t)(now-next_background)<0) {
            TickType_t wait=next_background-now;
            if(signal_service_pending() && wait>pdMS_TO_TICKS(200))wait=pdMS_TO_TICKS(200);
            (void)ulTaskNotifyTake(pdTRUE,wait);continue;
        }
        at_uart_stats_t st;
        at_uart_get_stats(&st);
        board_log("[probe] link check rx=%lu tx=%lu heap=%u",
                  (unsigned long)st.rx_bytes, (unsigned long)st.tx_bytes,
                  (unsigned)xPortGetFreeHeapSize());
        /* Only AT is needed while the module is silent. Once it answers,
         * repeat the identity/SIM/network suite to identify the recovered link.
         * Do not repeatedly toggle PWRKEY: the same key can shut a live module
         * down, and the board's power-control mapping is still unverified. */
        if (probe_cmd(cli, &s_steps[1]) == QOSA_OK) {
            if (pass == 0u) {
                board_log("[probe] AT link recovered; re-running module checks");
                pass = probe_suite(cli);
            } else {
                (void)probe_cmd(cli,&s_steps[7]);
                if(!voice_service_busy()) (void)probe_cmd(cli,&s_steps[8]);
            }
        } else {
            pass = 0;
            app_event_t event={.kind=APP_EVENT_PHONE};
            event.value.phone.kind=PHONE_EVENT_NETWORK;
            event.value.phone.source=PHONE_SOURCE_MODEM;
            event.value.phone.value.network.enabled=1;
            (void)app_event_post(&event);
        }
        next_background=xTaskGetTickCount()+pdMS_TO_TICKS(pass?30000u:5000u);
    }
}

void at_probe_start(void)
{
    BaseType_t ok = xTaskCreate(probe_task, "at_probe", PROBE_TASK_STACK_WORDS,
                                NULL, PROBE_TASK_PRIO, &s_probe_task);
    configASSERT(ok == pdPASS);
}
