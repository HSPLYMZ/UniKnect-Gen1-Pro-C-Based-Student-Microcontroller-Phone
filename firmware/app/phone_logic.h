#ifndef PHONE_LOGIC_H
#define PHONE_LOGIC_H
#include "phone_time.h"
#include "signal_data.h"
#include "settings_data.h"
#include <stddef.h>

#define PHONE_NUMBER_MAX 24u
#define PHONE_SMS_TEXT_MAX 240u
#define PHONE_SMS_SLOTS 4u
enum { PHONE_SOURCE_UI, PHONE_SOURCE_HOST, PHONE_SOURCE_MODEM, PHONE_SOURCE_DEMO };
enum { PHONE_CALL_IDLE, PHONE_CALL_DIALING, PHONE_CALL_ACTIVE, PHONE_CALL_INCOMING, PHONE_CALL_ENDED };
enum { PHONE_NOTE_NONE, PHONE_NOTE_OFFLINE, PHONE_NOTE_FULL, PHONE_NOTE_DEMO_DENIED,
       PHONE_NOTE_BUSY, PHONE_NOTE_NO_SERVICE, PHONE_NOTE_CALL_FAILED, PHONE_NOTE_EMPTY };
enum { PHONE_INTENT_NONE, PHONE_INTENT_DIAL, PHONE_INTENT_ANSWER,
       PHONE_INTENT_REJECT, PHONE_INTENT_HANGUP };
typedef struct { uint8_t kind; char number[PHONE_NUMBER_MAX+1]; uint32_t call_generation; } phone_intent_t;
enum { PHONE_KEY_UP=128, PHONE_KEY_DOWN, PHONE_KEY_LEFT, PHONE_KEY_RIGHT,
       PHONE_KEY_SELECT, PHONE_KEY_BACK, PHONE_KEY_CALL, PHONE_KEY_END,
       PHONE_KEY_MUTE, PHONE_KEY_SPEAKER, PHONE_KEY_DELETE, PHONE_KEY_HOME, PHONE_KEY_FOCUS,
       PHONE_KEY_SHOW_CALL, PHONE_KEY_SIGNAL_REFRESH, PHONE_KEY_SETTINGS_ROTATE, PHONE_KEY_CAL_START, PHONE_KEY_CAL_CANCEL, PHONE_KEY_SETTINGS_RETRY, PHONE_KEY_CAL_HOST_START }; /* internal, no implicit answer */
enum { PHONE_EVENT_KEY, PHONE_EVENT_NUMBER, PHONE_EVENT_CLOCK, PHONE_EVENT_NETWORK,
       PHONE_EVENT_SMS, PHONE_EVENT_CALL, PHONE_EVENT_DEMO, PHONE_EVENT_VOICE_RESULT,
       PHONE_EVENT_ACTION }; /* internal: key already resolved by control task */

#define PHONE_HISTORY_MAX 8u
#define PHONE_STATUS_H 16
#define PHONE_NAV_H 20
#define PHONE_CONTENT_BOTTOM 108
enum { PHONE_TOP_CLOCK, PHONE_TOP_CONTACT, PHONE_TOP_SMS, PHONE_TOP_SIGNAL, PHONE_TOP_SETTINGS, PHONE_TOP_COUNT };
typedef struct {
    uint8_t page, home, menu, keypad, message, scroll, action, diagnostic;
    uint8_t top, row, list, signal_scroll;
} phone_nav_entry_t;

typedef struct {
    uint64_t updated_ms;
    int16_t rsrp_dbm;
    uint8_t enabled, registered, lte, signal_valid;
} phone_network_t;
typedef struct {
    uint32_t id, utc;
    char sender[PHONE_NUMBER_MAX+1], text[PHONE_SMS_TEXT_MAX+1];
    uint8_t unread, source;
} phone_sms_t;
typedef struct {
    uint64_t changed_ms, connected_ms;
    uint32_t id, generation;
    char number[PHONE_NUMBER_MAX+1];
    uint8_t state, muted, speaker, has_connection;
} phone_call_t;
typedef struct {
    phone_clock_t clock;
    phone_network_t network;
    signal_state_t signal;
    settings_state_t system;
    phone_call_t call;
    phone_sms_t inbox[PHONE_SMS_SLOTS];
    char number[PHONE_NUMBER_MAX+1];
    uint8_t message_count, selected_message, message_scroll, keypad_selected, menu_selected;
    uint8_t demo, note, pending, pending_kind;
    uint32_t pending_id;
    uint8_t focus_bar, nav_selected, home_selected, action_selected, diagnostic_scroll;
    uint8_t navigation_locked, history_count,signal_rotation;
    uint8_t top_page, selected_row, list_scroll;
    int16_t top_position; /* unwrapped carousel target, in page widths */
    phone_nav_entry_t history[PHONE_HISTORY_MAX];
} phone_state_t;
typedef struct {
    uint8_t kind, source;
    union {
        uint8_t key, demo;
        uint32_t utc;
        char number[PHONE_NUMBER_MAX+1];
        struct { int16_t rsrp_dbm; uint8_t enabled, registered, lte, signal_valid; } network;
        phone_sms_t sms;
        struct { uint8_t state; char number[PHONE_NUMBER_MAX+1]; uint32_t id, generation; } call;
        struct { uint32_t request_id; uint8_t note; } result;
    } value;
} phone_event_t;

void phone_state_init(phone_state_t *phone);
/* Resolve region focus/navigation BEFORE asking for a modem intent. 0=consumed. */
uint8_t phone_resolve_key(phone_state_t *phone, uint8_t *page, uint8_t key);
uint8_t phone_route_key(phone_state_t *phone, uint8_t *page, uint8_t key, uint8_t source);
int phone_can_select(const phone_state_t *phone, uint8_t page);
void phone_navigate(phone_state_t *phone, uint8_t *page, uint8_t target);
int phone_top_index(uint8_t page);
uint8_t phone_top_page(unsigned index);
unsigned phone_message_visible(unsigned rotation);
unsigned phone_message_units(unsigned rotation);
unsigned phone_contact_count(void);
/* Display-name matching only; never used to authorize or route a call. */
int phone_is_family_number(const char *number);
int phone_confirm(phone_state_t *phone,uint8_t *page);
int phone_apply(phone_state_t *phone, uint8_t *page, unsigned rotation,
                const phone_event_t *event, uint64_t now_ms);
/* 1 submits an intent, -1 consumes a duplicate while pending, 0 is ordinary UI. */
int phone_intent_from_key(const phone_state_t *phone, uint8_t page, uint8_t key,
                          phone_intent_t *intent);
void phone_tick(phone_state_t *phone, uint64_t now_ms);
unsigned phone_signal_bars(const phone_network_t *network);
uint32_t phone_utf8_next(const char **text); /* invalid byte -> replacement glyph */
int phone_utf8_valid(const char *text, size_t capacity);
/* One display line, widths in 6-pixel units; CJK takes two units. */
size_t phone_wrap_line(const char *text, unsigned units, char *out, size_t capacity);
unsigned phone_wrapped_lines(const char *text, unsigned units);
unsigned phone_call_seconds(const phone_state_t *phone, uint64_t now_ms);
#endif
