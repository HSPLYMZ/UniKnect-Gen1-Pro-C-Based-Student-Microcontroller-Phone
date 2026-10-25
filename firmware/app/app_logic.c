#include "app_logic.h"
#include "app_config.h"
#include <string.h>

void app_model_init(app_model_t *m)
{
    memset(m, 0, sizeof *m);
    m->rotation_auto = !APP_PHONE_ONLY;
    m->page = APP_PAGE_HOME;
    m->lcd_profile = CFG_LCD_DEFAULT_PROFILE;
    if(APP_PHONE_ONLY) m->sd_state=APP_SD_DISABLED;
    phone_state_init(&m->phone);
}

int app_model_apply(app_model_t *m, const app_event_t *e)
{
    if (!m || !e) return 0;
    switch (e->kind) {
    case APP_EVENT_PHONE:
        return phone_apply(&m->phone,&m->page,m->rotation,&e->value.phone,m->uptime_ms);
    case APP_EVENT_JOY:
        if(e->value.joy.stage>6u) return 0;
        {
        if(!m->phone.system.cal_id && e->value.joy.stage==6u) {
            m->joy_idle=e->value.joy.idle;
            memcpy(m->joy_levels,e->value.joy.levels,sizeof m->joy_levels);
            m->joy_stage=6;m->joy_wait_release=e->value.joy.wait_release;
        }
        }
        break;
    case APP_EVENT_STORAGE:
        if(e->value.storage.state>APP_SD_DISABLED ||
           e->value.storage.free_mib>e->value.storage.total_mib) return 0;
        m->sd_state=e->value.storage.state;
        m->sd_free_mib=e->value.storage.free_mib;
        m->sd_total_mib=e->value.storage.total_mib;
        m->sd_writes=e->value.storage.writes;
        break;
    case APP_EVENT_LIGHT: {
        if (e->value.raw > 4095u) return 0;
        m->light_raw = e->value.raw;
        m->light_valid = 1;
        m->light_error = 0;
        ++m->light_samples;
        uint32_t scaled = (uint32_t)m->light_raw << 4;
        int transition = m->light_exposed ? scaled >= CFG_LIGHT_EXPOSED_CLEAR
                                         : scaled <= CFG_LIGHT_EXPOSED_TRIGGER;
        if (!transition) m->light_streak = 0;
        else if (++m->light_streak >= CFG_EVENT_CONSECUTIVE) {
            m->light_exposed = !m->light_exposed;
            m->light_streak = 0;
        }
        break;
    }
    case APP_EVENT_INPUT:
        if (e->value.raw > 4095u) return 0;
        m->input_raw = e->value.raw;
        m->input_valid = 1;
        m->input_error = 0;
        ++m->input_samples;
        break;
    case APP_EVENT_PAGE:
        if(m->phone.system.cal_id)return 0;
        if (e->value.page > APP_PAGE_COUNT) return 0;
        if(APP_PHONE_ONLY && e->value.page!=APP_PAGE_COUNT && e->value.page!=APP_PAGE_HOME &&
           e->value.page!=APP_PAGE_CONTACT_ENTRY && e->value.page!=APP_PAGE_SMS_ENTRY &&
           e->value.page!=APP_PAGE_SETTINGS_ENTRY && e->value.page!=APP_PAGE_SETTINGS_MENU &&
           e->value.page!=APP_PAGE_ROTATION && e->value.page!=APP_PAGE_CAL_CONFIRM &&
           e->value.page!=APP_PAGE_SIGNAL_ENTRY && e->value.page!=APP_PAGE_SIGNAL_INFO &&
           e->value.page!=APP_PAGE_CONTACTS && e->value.page!=APP_PAGE_INBOX &&
           e->value.page!=APP_PAGE_MESSAGE && e->value.page!=APP_PAGE_CALL &&
           e->value.page!=APP_PAGE_INCOMING && e->value.page!=APP_PAGE_JOYSTICK) return 0;
        if(APP_PHONE_ONLY && e->value.page==APP_PAGE_COUNT) {
            phone_navigate(&m->phone,&m->page,phone_top_page(m->phone.top_page+1u));break;
        }
        phone_navigate(&m->phone,&m->page,e->value.page==APP_PAGE_COUNT ?
            (m->page+1u)%APP_PAGE_COUNT : e->value.page);
        if(e->value.page==APP_PAGE_SIGNAL_INFO)m->phone.signal.refresh_requested=1;
        break;
    case APP_EVENT_LCD:
        m->lcd_ready = e->value.lcd.ready;
        m->lcd_error = e->value.lcd.error;
        m->lcd_flushed_cells = e->value.lcd.flushed_cells;
        m->lcd_rotation = e->value.lcd.rotation;
        break;
    case APP_EVENT_HEALTH:
        m->heap_free = e->value.health.heap_free;
        m->heap_min = e->value.health.heap_min;
        m->stale_mask = e->value.health.stale_mask;
        m->stack_low_mask = e->value.health.stack_low_mask;
        m->event_drops = e->value.health.event_drops;
        m->log_drops = e->value.health.log_drops;
        m->rx_drops = e->value.health.rx_drops;
        m->idle_loops = e->value.health.idle_loops;
        break;
    case APP_EVENT_LCD_RETRY:
        ++m->lcd_retry_generation;
        break;
    case APP_EVENT_LCD_CONFIG:
        if(e->value.page>17u) return 0;
        m->lcd_profile=e->value.page; ++m->lcd_retry_generation;
        break;
    case APP_EVENT_LCD_BENCH:
        if(e->value.page>6u || m->phone.system.cal_id) return 0;
        m->lcd_bench=e->value.page;
        break;
    case APP_EVENT_LIGHT_FAULT:
        m->light_valid = 0;
        m->light_error = 1;
        m->light_streak = 0;
        break;
    case APP_EVENT_INPUT_FAULT:
        m->input_valid = 0;
        m->input_error = 1;
        break;
    case APP_EVENT_MOTION:
        if (e->value.motion.rotation > 3) return 0;
        m->motion_valid = !!e->value.motion.valid;
        m->motion_error = e->value.motion.error;
        m->motion_flat = e->value.motion.flat;
        m->motion_pending = e->value.motion.pending;
        m->motion_usable = e->value.motion.usable;
        if (m->motion_valid) {
            m->accel_x_mg = e->value.motion.x_mg;
            m->accel_y_mg = e->value.motion.y_mg;
            m->accel_z_mg = e->value.motion.z_mg;
            m->motion_rotation = e->value.motion.rotation;
            ++m->motion_samples;
            if (m->rotation_auto && m->motion_usable && !m->motion_pending && !m->motion_flat)
                m->rotation = m->motion_rotation;
        }
        break;
    case APP_EVENT_ROTATION_MODE:
        if(m->phone.system.cal_id)return 0;
        if (e->value.rotation_mode.rotation > 3 || e->value.rotation_mode.automatic > 1) return 0;
        m->rotation_auto = e->value.rotation_mode.automatic;
        if (!m->rotation_auto) m->rotation = e->value.rotation_mode.rotation;
        else if (m->motion_valid && m->motion_usable && !m->motion_pending && !m->motion_flat)
            m->rotation = m->motion_rotation;
        break;
    default: return 0;
    }
    m->phone.signal_rotation=m->rotation;
    return 1;
}

app_command_t app_command_parse(const char *line)
{
    app_command_t cmd = {0};
    if (!line) return cmd;
    /* Fixed upper bound even when called outside the UART line collector. */
    char text[64];
    size_t n = 0;
    while (line[n] && n < sizeof text - 1u) { text[n] = line[n]; ++n; }
    if (line[n]) return cmd;
    while (n && (text[n-1] == ' ' || text[n-1] == '\t')) --n;
    text[n] = 0;
    char *s = text;
    while (*s == ' ' || *s == '\t') ++s;
    if (!strcmp(s, "help")) cmd.kind = APP_CMD_HELP;
    else if (!strcmp(s, "status")) cmd.kind = APP_CMD_STATUS;
    else if (!strcmp(s, "tasks")) cmd.kind = APP_CMD_TASKS;
    else if (!strcmp(s,"signal status"))cmd.kind=APP_CMD_SIGNAL_STATUS;
    else if (!strcmp(s,"signal refresh")) {
        cmd.kind=APP_CMD_PHONE;cmd.phone.kind=PHONE_EVENT_ACTION;
        cmd.phone.source=PHONE_SOURCE_HOST;cmd.phone.value.key=PHONE_KEY_SIGNAL_REFRESH;
    }
    else if (!strcmp(s, "lcd retry")) cmd.kind = APP_CMD_LCD_RETRY;
    else if (!strcmp(s, "motion")) cmd.kind = APP_CMD_MOTION;
    else if (!strcmp(s,"settings status"))cmd.kind=APP_CMD_SETTINGS_STATUS;
    else if (!strcmp(s,"joy cancel") || !strcmp(s,"settings retry")) {
        cmd.kind=APP_CMD_PHONE;cmd.phone.kind=PHONE_EVENT_ACTION;cmd.phone.source=PHONE_SOURCE_HOST;
        cmd.phone.value.key=!strcmp(s,"joy cancel")?PHONE_KEY_CAL_CANCEL:PHONE_KEY_SETTINGS_RETRY;
    }
    else if (!strcmp(s, "joy calibrate")) cmd.kind = APP_CMD_JOY_CALIBRATE;
    else if (!strncmp(s,"clock ",6)) {
        uint32_t utc;
        if (phone_time_parse(s+6,&utc)) {
            cmd.kind=APP_CMD_PHONE; cmd.phone.kind=PHONE_EVENT_CLOCK;
            cmd.phone.source=PHONE_SOURCE_HOST; cmd.phone.value.utc=utc;
        }
    }
    else if (!APP_PHONE_ONLY && !strncmp(s,"number ",7) && strlen(s+7)<=PHONE_NUMBER_MAX) {
        cmd.kind=APP_CMD_PHONE; cmd.phone.kind=PHONE_EVENT_NUMBER;
        cmd.phone.source=PHONE_SOURCE_HOST;
        strcpy(cmd.phone.value.number,s+7);
        for (const char *p=s+7;*p;++p)
            if ((*p<'0' || *p>'9') && *p!='*' && *p!='#' && !(*p=='+' && p==s+7)) cmd.kind=APP_CMD_INVALID;
    }
    else if (!APP_PHONE_ONLY && (!strcmp(s,"demo on") || !strcmp(s,"demo off"))) {
        cmd.kind=APP_CMD_PHONE; cmd.phone.kind=PHONE_EVENT_DEMO; cmd.phone.value.demo=!strcmp(s,"demo on");
    }
    else if (!APP_PHONE_ONLY && !strcmp(s,"demo ring")) {
        cmd.kind=APP_CMD_PHONE; cmd.phone.kind=PHONE_EVENT_CALL; cmd.phone.source=PHONE_SOURCE_DEMO;
        cmd.phone.value.call.state=PHONE_CALL_INCOMING; strcpy(cmd.phone.value.call.number,"13800138000");
    }
    else if (!strncmp(s,"lcd profile ",12)) {
        static const char *const profiles[]={"gpio","fast","poll7","poll6","poll5","poll4","poll3","poll2","poll1","poll0",
            "dma7","dma6","dma5","dma4","dma3","dma2","dma1","dma0"};
        for(unsigned i=0;i<sizeof profiles/sizeof profiles[0];++i)
            if(!strcmp(s+12,profiles[i])) { cmd.kind=APP_CMD_LCD_CONFIG; cmd.page=i; }
    }
    else if (!strncmp(s,"lcd bench ",10)) {
        static const char *const modes[]={"off","carousel","list","scroll","color","signal","settings"};
        for(unsigned i=0;i<7;++i) if(!strcmp(s+10,modes[i])) { cmd.kind=APP_CMD_LCD_BENCH; cmd.page=i; }
        if(!strcmp(s+10,"menu")) {cmd.kind=APP_CMD_LCD_BENCH;cmd.page=1;}
        if(!strcmp(s+10,"nav")) {cmd.kind=APP_CMD_LCD_BENCH;cmd.page=2;}
    }
    else if (!strncmp(s,"key ",4)) {
        static const char *const names[]={"up","down","left","right","select","back","call","end","mute","speaker","delete","home","focus"};
        for (unsigned i=0;i<sizeof names/sizeof names[0];++i)
            if (!strcmp(s+4,names[i])) { cmd.kind=APP_CMD_PHONE; cmd.phone.kind=PHONE_EVENT_KEY; cmd.phone.source=PHONE_SOURCE_HOST; cmd.phone.value.key=PHONE_KEY_UP+i; }
        if (!APP_PHONE_ONLY && strlen(s+4)==1 && ((s[4]>='0' && s[4]<='9') || s[4]=='*' || s[4]=='#')) {
            cmd.kind=APP_CMD_PHONE; cmd.phone.kind=PHONE_EVENT_KEY; cmd.phone.value.key=(uint8_t)s[4];
            cmd.phone.source=PHONE_SOURCE_HOST;
        }
    }
    else {
        static const char *const pages[] = {"page status", "page tasks", "page io", "page home", "page menu",
            "page dialer", "page inbox", "page message", "page call", "page incoming",
            "page joystick", "page contact", "page sms", "page contacts", "page signal", "page signalinfo", "page settings", "page settingsmenu", "page rotation", "page calibration", "page next"};
        _Static_assert(sizeof pages / sizeof pages[0] == APP_PAGE_COUNT + 1u,
                       "Page command table mismatch");
        for (uint8_t i = 0; i <= APP_PAGE_COUNT; ++i)
            if (!strcmp(s, pages[i])) {
                if(APP_PHONE_ONLY && (i<=APP_PAGE_IO || i==APP_PAGE_MENU || i==APP_PAGE_DIALER)) break;
                cmd.kind = APP_CMD_PAGE; cmd.page = i; break;
            }
        static const char *const rotation[] = {"rotate 0", "rotate 90", "rotate 180", "rotate 270", "rotate auto"};
        for (uint8_t i = 0; i < 5; ++i)
            if (!strcmp(s, rotation[i])) { cmd.kind = APP_CMD_ROTATE; cmd.page = i; break; }
    }
    return cmd;
}

int app_heartbeat_late(uint32_t now, uint32_t last, uint32_t deadline)
{
    return (uint32_t)(now - last) > deadline;
}

int app_line_feed(app_line_t *line, int c)
{
    if (c == -2) { line->used = 0; line->dropping = 1; return 0; }
    if (c < 0) return 0;
    if (c == '\r' || c == '\n') {
        int result = line->dropping ? -1 : line->used ? 1 : 0;
        line->text[line->used] = 0;
        line->used = 0; line->dropping = 0;
        return result;
    }
    if (c == '\b' || c == 127) {
        if (line->used && !line->dropping) --line->used;
    } else if ((c >= 32 && c <= 126) || c == '\t') {
        if (line->used < sizeof line->text - 1u && !line->dropping)
            line->text[line->used++] = c == '\t' ? ' ' : (char)c;
        else line->dropping = 1;
    } else line->dropping = 1;
    return 0;
}
