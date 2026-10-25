#include "phone_probe.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "at.h"
#include "board.h"
#include "qosa_def.h"

#define PHONE_TASK_STACK_WORDS  768u
#define PHONE_TASK_PRIO         3u

/* at_probe brings the client up; give it room to finish its own 8 commands. */
#define PHONE_START_DELAY_MS    4000u

#define PHONE_RESP_SIZE         256u
#define PHONE_CMD_TIMEOUT_MS    5000
#define PHONE_DIAL_TIMEOUT_MS   15000

/* Talk for this long, then hang up. Long enough for the handset to ring and be
 * answered, short enough not to look like an abandoned call. */
#define PHONE_TALK_MS           12000u

/* The number to try. This is the only number this build is allowed to dial;
 * it is the owner's handset, so answering it proves the audio path both ways
 * instead of just "the module accepted the command". */
#define PHONE_TEST_NUMBER       "19273412328"

/* ^DSCI state codes, from Quectel's ql_voice_manager.h. */
static const char *dsci_state_name(int state)
{
    switch (state) {
    case 1: return "HOLD";
    case 2: return "ORIGINATED";
    case 3: return "CONNECTED";
    case 4: return "INCOMING";
    case 5: return "WAITING";
    case 6: return "ENDED";
    case 7: return "ALERTING";
    default: return "?";
    }
}

static volatile int s_last_state = -1;
static volatile int s_dial_started;

/* Runs in the AT parser task. Only record and log; never issue AT commands
 * from here (the client holds its own lock). */
static void dsci_urc(struct at_client *client, const char *data, size_t size, void *arg)
{
    (void)client;
    (void)size;
    (void)arg;

    int id = 0;
    int dir = 0;
    int state = 0;
    char number[32] = {0};

    int n = sscanf(data, "^DSCI: %d,%d,%d,%*d,%31[^,]", &id, &dir, &state, number);
    if (n < 3) {
        board_log("[ph] URC unparsed: %s", data);
        return;
    }

    s_last_state = state;
    board_log("[ph] URC DSCI id=%d dir=%s state=%d(%s) num=%s",
              id, (dir == 0) ? "MO" : "MT", state, dsci_state_name(state),
              (n >= 4 && number[0] != '\0') ? number : "-");
}

static const struct at_urc s_urc_table[] = {
    {"^DSCI:", "\r\n", dsci_urc},
};

/* One query, prints every response line. */
/* 3GPP +CLCC call states. This is the portable way to follow a call: unlike the
 * vendor ^DSCI URC it is defined by the standard and works on any firmware. */
static const char *clcc_state_name(int stat)
{
    switch (stat) {
    case 0: return "ACTIVE";
    case 1: return "HELD";
    case 2: return "DIALING";
    case 3: return "ALERTING";
    case 4: return "INCOMING";
    case 5: return "WAITING";
    default: return "?";
    }
}

/* Returns the state of the first call in a +CLCC response, or -1 if the module
 * reports no current calls. */
static int clcc_first_state(at_response_t resp)
{
    for (size_t i = 1; i <= resp->line_counts; ++i) {
        const char *line = at_resp_get_line(resp, i);
        if (line == NULL || strncmp(line, "+CLCC:", 6) != 0) {
            continue;
        }
        int id = 0;
        int dir = 0;
        int stat = 0;
        if (sscanf(line, "+CLCC: %d,%d,%d", &id, &dir, &stat) >= 3) {
            return stat;
        }
    }
    return -1;
}

static int phone_cmd(at_client_t cli, const char *cmd, int timeout_ms)
{
    at_response_t resp = at_create_resp(PHONE_RESP_SIZE, 0, timeout_ms);
    if (resp == NULL) {
        board_log("[ph] %-16s no memory for response", cmd);
        return -1;
    }

    int rc = at_obj_exec_cmd(cli, resp, "%s", cmd);
    board_log("[ph] %-16s %s", cmd, (rc == QOSA_OK) ? "OK" : "FAIL");

    for (size_t i = 1; i <= resp->line_counts; ++i) {
        const char *line = at_resp_get_line(resp, i);
        if (line != NULL && line[0] != '\0') {
            board_log("[ph]   | %s", line);
        }
    }

    at_delete_resp(resp);
    return rc;
}

static void phone_probe_task(void *arg)
{
    (void)arg;

    vTaskDelay(pdMS_TO_TICKS(PHONE_START_DELAY_MS));

    at_client_t cli = at_client_get_first();
    if (cli == NULL || cli->status != AT_STATUS_INITIALIZED) {
        board_log("[ph] no AT client; is at_probe running?");
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(10000));
        }
    }

    if (at_obj_set_urc_table(cli, s_urc_table,
                             sizeof s_urc_table / sizeof s_urc_table[0]) != QOSA_OK) {
        board_log("[ph] failed to register ^DSCI URC");
    }

    board_log("[ph] ===== voice bring-up =====");

    /* Registration first: without it the dial result is meaningless. */
    phone_cmd(cli, "AT+CPIN?", 3000);
    phone_cmd(cli, "AT+COPS?", 5000);
    phone_cmd(cli, "AT+CEREG?", 3000);
    phone_cmd(cli, "AT+CSQ", 3000);
    phone_cmd(cli, "AT+CCLK?", 3000);
    /* VoLTE switch: EC200U needs IMS enabled for voice over LTE. */
    phone_cmd(cli, "AT+QCFG=\"ims\"", 3000);
    phone_cmd(cli, "AT+CLVL?", 3000);

    /* Registering the ^DSCI handler only tells the client how to parse it; the
     * module still has to be told to emit it. Without this line the call state
     * stream stays silent even though ATD returns OK. */
    phone_cmd(cli, "AT^DSCI=1", 3000);

    /* Let the module report local time instead of UTC. */
    phone_cmd(cli, "AT+CTZU=1", 3000);

    board_log("[ph] ---- dialling %s, will hang up after %u ms ----",
              PHONE_TEST_NUMBER, (unsigned)PHONE_TALK_MS);

    s_dial_started = 1;
    s_last_state = -1;

    /* Trailing semicolon makes it a voice call; without it this is a data call. */
    at_response_t resp = at_create_resp(PHONE_RESP_SIZE, 0, PHONE_DIAL_TIMEOUT_MS);
    int rc = (resp != NULL) ? at_obj_exec_cmd(cli, resp, "ATD%s;", PHONE_TEST_NUMBER) : -1;
    if (resp != NULL) {
        for (size_t i = 1; i <= resp->line_counts; ++i) {
            const char *line = at_resp_get_line(resp, i);
            if (line != NULL && line[0] != '\0') {
                board_log("[ph]   | %s", line);
            }
        }
        at_delete_resp(resp);
    }

    if (rc != QOSA_OK) {
        board_log("[ph] DIAL result uncertain rc=%d; querying and cancelling", rc);
        phone_cmd(cli, "AT+CLCC", 3000);
        int hangup = phone_cmd(cli, "ATH", 5000);
        if (hangup != QOSA_OK) hangup = phone_cmd(cli, "ATH", 5000);
        board_log("[ph] verdict: DIAL FAILED/UNKNOWN, hangup=%s",
                  hangup == QOSA_OK ? "accepted" : "unconfirmed");
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(30000));
        }
    }

    board_log("[ph] dial accepted (OK). polling +CLCC every 1.5 s ...");

    int connected = 0;
    int alerting = 0;
    int dialing = 0;
    int polls = 0;
    uint32_t start = xTaskGetTickCount();
    uint32_t next_poll = start;

    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(PHONE_TALK_MS)) {
        if ((int32_t)(xTaskGetTickCount() - next_poll) >= 0) {
            next_poll += pdMS_TO_TICKS(1500);

            at_response_t r = at_create_resp(PHONE_RESP_SIZE, 0, 3000);
            if (r != NULL) {
                int query_rc = at_obj_exec_cmd(cli, r, "AT+CLCC");
                int st = clcc_first_state(r);
                polls++;
                if (query_rc == QOSA_OK && st >= 0) {
                    board_log("[ph] CLCC#%d state=%d(%s)", polls, st,
                              clcc_state_name(st));
                    if (st == 0) {
                        connected = 1;
                    } else if (st == 3) {
                        alerting = 1;
                    } else if (st == 2) {
                        dialing = 1;
                    }
                } else if (query_rc == QOSA_OK) {
                    board_log("[ph] CLCC#%d no current call", polls);
                } else {
                    board_log("[ph] CLCC#%d query failed rc=%d", polls, query_rc);
                }
                at_delete_resp(r);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    board_log("[ph] hanging up (connected=%d alerting=%d dialing=%d)",
              connected, alerting, dialing);
    int hangup_rc = phone_cmd(cli, "ATH", 5000);
    if (hangup_rc != QOSA_OK) hangup_rc = phone_cmd(cli, "ATH", 5000);
    if (hangup_rc != QOSA_OK) board_log("[ph] WARNING: hangup unconfirmed");

    vTaskDelay(pdMS_TO_TICKS(1500));

    /* If the call never got anywhere, ask the module why. */
    if (!connected && !alerting) {
        phone_cmd(cli, "AT+CEER", 3000);
    }

    board_log("[ph] ===== verdict =====");
    if (connected) {
        board_log("[ph] CONNECTED -- call active; two-way audio needs a listening check.");
    } else if (alerting) {
        board_log("[ph] ALERTING -- call reached the handset and rang, but was");
        board_log("[ph] not answered within %u ms. voice path is OK.",
                  (unsigned)PHONE_TALK_MS);
    } else if (dialing) {
        board_log("[ph] DIALING only -- the network never rang the handset.");
        board_log("[ph] likely no voice service on this SIM or no VoLTE bearer.");
    } else if (polls > 0) {
        board_log("[ph] NO CALL OBJECT -- ATD returned OK but no call existed.");
        board_log("[ph] see AT+CEER above for the network reason.");
    } else {
        board_log("[ph] CLCC never ran -- AT channel is stuck.");
    }

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

void phone_probe_start(void)
{
    BaseType_t ok = xTaskCreate(phone_probe_task, "phone", PHONE_TASK_STACK_WORDS,
                                NULL, PHONE_TASK_PRIO, NULL);
    configASSERT(ok == pdPASS);
}
