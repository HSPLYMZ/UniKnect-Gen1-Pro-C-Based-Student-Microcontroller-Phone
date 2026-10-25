#include "common.h"
#include "../../firmware/app/orientation.c"

static uint32_t now;
static void hold(orientation_t *s, int16_t x, int16_t y, int16_t z, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) { orientation_update(s,x,y,z,now); now += 50; }
}
void test_main(void)
{
    orientation_t s;
    orientation_init(&s);
    hold(&s,0,1000,0,20); CHECK(s.rotation == 0 && s.usable);
    hold(&s,-1000,0,0,6); CHECK(s.rotation == 0); /* filtered + dwell */
    hold(&s,-1000,0,0,30); CHECK(s.rotation == 3 && s.usable);
    hold(&s,0,-1000,0,40); CHECK(s.rotation == 2);
    hold(&s,1000,0,0,40); CHECK(s.rotation == 1);
    hold(&s,0,0,1000,40); CHECK(s.rotation == 1 && s.flat && !s.usable);
    hold(&s,0,0,-1000,40); CHECK(s.rotation == 1 && s.flat);
    hold(&s,0,0,0,20); CHECK(s.rotation == 1 && !s.usable);
    hold(&s,2000,0,0,20); CHECK(s.rotation == 1 && !s.pending);
    hold(&s,707,707,0,30); CHECK(s.rotation == 1 && !s.usable); /* diagonal */
    /* Interrupted candidates must not accumulate their stable duration. */
    orientation_init(&s);
    for (unsigned i = 0; i < 20; ++i) {
        orientation_invalidate(&s);
        hold(&s,-1000,0,0,4);
        CHECK(s.rotation == 0);
    }
    hold(&s,-1000,0,0,30); CHECK(s.rotation == 3);
    orientation_invalidate(&s); CHECK(s.rotation == 3 && !s.pending);
    /* Stable confirmation crosses tick wrap. */
    orientation_init(&s); now = 0xffffff00u;
    hold(&s,1000,0,0,11); CHECK(s.rotation == 1);
    /* Invalid int16 extremes cannot overflow the norm test. */
    hold(&s,-32768,-32768,-32768,2); CHECK(s.rotation == 1 && !s.usable);
    test_finish(0);
}
