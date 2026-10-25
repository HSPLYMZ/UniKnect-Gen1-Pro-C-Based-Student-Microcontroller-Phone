#include "common.h"
#include "../../firmware/app/joy_input.c"
static joy_input_t s;
static joy_dir_t sample(uint16_t value) {return joy_input_feed(&s,value,1);}
static void quiet(uint16_t value,unsigned count) {for(unsigned i=0;i<count;++i) CHECK(sample(value)==JOY_NONE);}
static void release(void) {quiet(4065,4);}
void test_main(void)
{
    joy_input_use_fixed(&s);CHECK(s.stage==6 && s.idle==4065 && s.wait_release);
    quiet(737,300);CHECK(s.wait_release);release();CHECK(!s.wait_release);
    const uint16_t levels[]={2046,88,3720,737,1308};
    const joy_dir_t directions[]={JOY_UP,JOY_DOWN,JOY_LEFT,JOY_RIGHT,JOY_CENTER};
    for(unsigned i=0;i<5;++i) {
        quiet(levels[i],3);CHECK(sample(levels[i])==directions[i]);quiet(levels[i],600);
        release();quiet(levels[i],3);CHECK(sample(levels[i])==directions[i]);release();
    }
    quiet(737,2);quiet(4065,4);quiet(737,3);CHECK(sample(737)==JOY_RIGHT);
    quiet(1308,20);release();
    quiet(2700,4);CHECK(s.wait_release);quiet(737,10);release();
    quiet(737,2);CHECK(joy_input_feed(&s,0,0)==JOY_NONE);
    quiet(737,10);release();quiet(737,3);CHECK(sample(737)==JOY_RIGHT);release();
    quiet(737,1);quiet(2046,8);CHECK(s.wait_release);release();
    quiet(1308,3);CHECK(sample(1308)==JOY_CENTER);quiet(1308,1000);release();
    for(unsigned r=0;r<4;++r) {CHECK(joy_screen_direction(JOY_CENTER,0,r)==JOY_CENTER);CHECK(joy_screen_direction(JOY_UP,r,r)==JOY_UP);}
    CHECK(joy_screen_direction(JOY_UP,0,1)==JOY_LEFT);
    joy_input_reset(&s);quiet(4070,16);CHECK(s.stage==1);
    const uint16_t manual[]={3500,3100,2600,2200,1700};
    for(unsigned i=0;i<5;++i) {quiet(manual[i],8);CHECK(s.wait_release);quiet(4070,5);}
    CHECK(s.stage==6);test_finish(0);
}
