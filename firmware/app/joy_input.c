#include "joy_input.h"
#include <string.h>
#include "app_config.h"

enum { IDLE_MIN=3800, IDLE_JITTER=24, PRESS_DELTA=120,
       PLATFORM_JITTER=24, PLATFORM_GAP=80, RELEASE_DELTA=70,
       IDLE_SAMPLES=16, PRESS_SAMPLES=8, RELEASE_SAMPLES=5,
       DEBOUNCE_SAMPLES=(CFG_JOY_STABLE_MS+CFG_JOY_SCAN_MS-1u)/CFG_JOY_SCAN_MS,
       RELEASE_NORMAL=(CFG_JOY_RELEASE_MS+CFG_JOY_SCAN_MS-1u)/CFG_JOY_SCAN_MS };

static unsigned distance(unsigned a, unsigned b) { return a>b ? a-b : b-a; }

void joy_input_reset(joy_input_t *s) { memset(s,0,sizeof *s); }

void joy_input_use_fixed(joy_input_t *s)
{
    joy_input_reset(s);
    /* Adafruit 1.8-inch analogue shield ladder, also used by this shield
     * family. Idle was measured near 4065 on the assembled STM32 board. */
    static const uint16_t levels[5]={2046,88,3720,737,1308};
    s->idle=4065;
    memcpy(s->levels,levels,sizeof levels);
    s->stage=6;
    s->fixed_levels=1;
    s->wait_release=1; /* Never interpret a key held across boot as a press. */
}

static joy_dir_t classify(const joy_input_t *s, uint16_t raw)
{
    if(distance(raw,s->idle)<PRESS_DELTA) return JOY_NONE;
    unsigned best=0xffffu, second=0xffffu;
    int index=-1;
    for(unsigned i=0;i<5;++i) {
        unsigned d=distance(raw,s->levels[i]);
        if(d<best) { second=best;best=d;index=(int)i; }
        else if(d<second) second=d;
    }
    /* Reject both an unknown analogue platform and the midpoint between two
     * measured platforms. This avoids guessing a direction on marginal ADC. */
    if(best>(s->fixed_levels?180u:80u) || second-best<20u) return JOY_NONE;
    return (joy_dir_t)(JOY_UP+index);
}

joy_dir_t joy_input_feed(joy_input_t *s, uint16_t raw, int valid)
{
    if(!valid || raw>4095u) {
        s->pending=JOY_NONE;s->debounce=0;s->held=JOY_NONE;s->held_ticks=0;
        s->stable=0;s->released=0;
        if(s->stage==6u) s->wait_release=1;
        return JOY_NONE;
    }
    if(s->stage==0) {
        if(raw<IDLE_MIN) { s->stable=0;return JOY_NONE; }
        if(!s->stable || distance(raw,s->candidate)>IDLE_JITTER) {
            s->candidate=raw;s->stable=1;
        } else if(++s->stable>=IDLE_SAMPLES) {
            s->idle=s->candidate;s->stage=1;s->stable=0;
        }
        return JOY_NONE;
    }
    if(s->stage<6) {
        if(s->wait_release) {
            if(distance(raw,s->idle)<RELEASE_DELTA) {
                if(++s->released>=RELEASE_SAMPLES) {
                    s->wait_release=0;s->released=0;s->stable=0;
                    ++s->stage;
                }
            } else s->released=0;
            return JOY_NONE;
        }
        if(distance(raw,s->idle)<PRESS_DELTA) { s->stable=0;return JOY_NONE; }
        if(!s->stable || distance(raw,s->candidate)>PLATFORM_JITTER) {
            s->candidate=raw;s->stable=1;return JOY_NONE;
        }
        if(++s->stable<PRESS_SAMPLES) return JOY_NONE;
        for(unsigned i=0;i<s->stage-1u;++i)
            if(distance(s->candidate,s->levels[i])<PLATFORM_GAP) {
                s->stable=0;return JOY_NONE;
            }
        s->levels[s->stage-1u]=s->candidate;
        s->wait_release=1;s->released=0;s->stable=0;
        return JOY_NONE;
    }
    int idle=distance(raw,s->idle)<RELEASE_DELTA;
    if(s->wait_release || s->held!=JOY_NONE) {
        if(idle) {
            if(++s->released>=RELEASE_NORMAL) {
                s->held=s->pending=JOY_NONE; s->wait_release=0;
                s->released=s->debounce=0;
            }
        } else s->released=0;
        return JOY_NONE;
    }
    joy_dir_t direction=classify(s,raw);
    if(idle) { s->pending=JOY_NONE; s->debounce=0; return JOY_NONE; }
    if(direction==JOY_NONE || (s->pending!=JOY_NONE && s->pending!=direction)) {
        s->wait_release=1; s->pending=JOY_NONE; s->debounce=s->released=0;
        return JOY_NONE;
    }
    if(s->pending!=direction) { s->pending=(uint8_t)direction; s->debounce=1; }
    else if(++s->debounce>=DEBOUNCE_SAMPLES) {
        s->held=(uint8_t)direction; s->held_ticks=0; s->debounce=0;
        return direction;
    }
    return JOY_NONE;
}

joy_dir_t joy_screen_direction(joy_dir_t dir,unsigned calibration_rotation,
                               unsigned current_rotation)
{
    if(dir<JOY_UP || dir>JOY_RIGHT || calibration_rotation>3u || current_rotation>3u)
        return dir;
    /* A calibrated UP is the visible UP at the calibration angle. The inverse
     * panel transform converts it to the current visible direction. */
    unsigned delta=(calibration_rotation-current_rotation)&3u;
    static const joy_dir_t turn[4][4] = {
        {JOY_UP,JOY_DOWN,JOY_LEFT,JOY_RIGHT},
        {JOY_RIGHT,JOY_LEFT,JOY_UP,JOY_DOWN},
        {JOY_DOWN,JOY_UP,JOY_RIGHT,JOY_LEFT},
        {JOY_LEFT,JOY_RIGHT,JOY_DOWN,JOY_UP}
    };
    return turn[delta][dir-JOY_UP];
}
