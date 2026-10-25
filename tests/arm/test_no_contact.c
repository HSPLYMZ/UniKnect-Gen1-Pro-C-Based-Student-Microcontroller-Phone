#include "common.h"
#include <string.h>
#include "../../firmware/app/phone_time.c"
#include "../../firmware/app/phone_logic.c"
#include "../../firmware/app/app_logic.c"
void test_main(void)
{
    app_model_t m;app_model_init(&m);CHECK(!*CFG_GUARDIAN_PHONE_NUMBER && !phone_contact_count());
    phone_navigate(&m.phone,&m.page,APP_PAGE_CONTACT_ENTRY);
    CHECK(!phone_resolve_key(&m.phone,&m.page,PHONE_KEY_SELECT) && m.page==APP_PAGE_CONTACTS);
    CHECK(!phone_can_select(&m.phone,m.page));phone_intent_t intent;
    CHECK(!phone_intent_from_key(&m.phone,m.page,PHONE_KEY_SELECT,&intent));
    CHECK(!phone_intent_from_key(&m.phone,m.page,PHONE_KEY_CALL,&intent));
    CHECK(!phone_is_family_number("") && !phone_is_family_number("12345678901"));
    m.phone.call.state=PHONE_CALL_INCOMING;
    CHECK(phone_intent_from_key(&m.phone,APP_PAGE_INCOMING,PHONE_KEY_CALL,&intent)==1 && intent.kind==PHONE_INTENT_ANSWER);
    test_finish(0);
}
