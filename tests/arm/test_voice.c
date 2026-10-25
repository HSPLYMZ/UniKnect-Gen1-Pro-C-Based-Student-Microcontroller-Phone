#include "common.h"
#include <string.h>
#include "../../firmware/app/phone_time.c"
#include "../../firmware/app/phone_logic.c"
#include "../../firmware/app/voice_logic.c"

void test_main(void)
{
    voice_snapshot_t call;
    CHECK(voice_parse_clcc("+CLCC: 2,1,4,0,0,\"13800138000\",129",&call));
    CHECK(call.id==2 && call.state==PHONE_CALL_INCOMING && call.incoming && !strcmp(call.number,"13800138000"));
    CHECK(!voice_parse_clcc("+CLCC: 3,1,0,1,0,\"\",128",&call));
    CHECK(voice_parse_clcc("+CLCC: 3,1,5,0,0,\"123\",129",&call) && call.waiting);
    CHECK(voice_parse_clcc("+CLCC: 1,0,0,0,0,\"10086\",129",&call));
    CHECK(call.state==PHONE_CALL_ACTIVE);
    CHECK(voice_parse_clcc("+CLCC: 1,0,2,0,0",&call));
    CHECK(call.state==PHONE_CALL_DIALING && !*call.number);
    CHECK(voice_parse_dsci("^DSCI: 1,0,2,0,10086,129",&call));
    CHECK(call.state==PHONE_CALL_DIALING && !strcmp(call.number,"10086"));
    CHECK(voice_parse_dsci("^DSCI: 1,0,3,0,10086,129",&call));
    CHECK(call.state==PHONE_CALL_ACTIVE);
    CHECK(voice_parse_dsci("^DSCI: 1,0,6,0,10086,129",&call));
    CHECK(call.id==1 && call.state==PHONE_CALL_ENDED);
    CHECK(!voice_parse_dsci("^DSCI: 1,0,3,1,10086,129",&call));
    CHECK(voice_parse_dsci("^DSCI: 2,1,5,0,123,129",&call) && call.waiting);
    CHECK(voice_parse_dsci("^DSCI: 2,1,4,0,,129",&call));
    CHECK(call.state==PHONE_CALL_INCOMING && !*call.number);
    char number[PHONE_NUMBER_MAX+1];
    CHECK(voice_parse_clip("+CLIP: \"+8613800138000\",145",number));
    CHECK(!strcmp(number,"+8613800138000"));
    CHECK(voice_parse_clip("+CLIP: \"\",128",number) && !*number);
    CHECK(!voice_parse_clip("+CLIP: \"12;ATD3\",129",number));
    test_finish(0);
}
