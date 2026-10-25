#include "settings_data.h"
#include <string.h>
static unsigned gap(unsigned a,unsigned b) {return a>b?a-b:b-a;}
void settings_defaults(settings_config_t *c)
{
    memset(c,0,sizeof *c);c->idle=4065;
    const uint16_t values[]={2046,88,3720,737,1308};memcpy(c->levels,values,sizeof values);
}
int settings_valid(const settings_config_t *c)
{
    if(!c || c->rotation>3 || c->calibration_rotation>3 || c->manual>1 || c->idle<3800 || c->idle>4095)return 0;
    for(unsigned i=0;i<5;++i) {
        if(c->levels[i]>4095 || gap(c->levels[i],c->idle)<120)return 0;
        for(unsigned j=0;j<i;++j)if(gap(c->levels[i],c->levels[j])<80)return 0;
    }
    return 1;
}
int settings_equal(const settings_config_t *a,const settings_config_t *b)
{
    return a->idle==b->idle && !memcmp(a->levels,b->levels,sizeof a->levels) &&
        a->rotation==b->rotation && a->calibration_rotation==b->calibration_rotation && a->manual==b->manual;
}
