#include "phone_time.h"
#include <string.h>

static int leap(unsigned y) { return !(y%4) && (y%100 || !(y%400)); }
static unsigned month_days(unsigned y, unsigned m)
{
    static const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return days[m-1] + (m == 2 && leap(y));
}
int phone_time_from_beijing(const phone_datetime_t *t, uint32_t *utc)
{
    if (!t || !utc || t->year < 2000 || t->year > 2099 || !t->month || t->month > 12 ||
        !t->day || t->day > month_days(t->year,t->month) || t->hour > 23 ||
        t->minute > 59 || t->second > 59) return 0;
    uint32_t days = 0;
    for (unsigned y = 1970; y < t->year; ++y) days += leap(y) ? 366 : 365;
    for (unsigned m = 1; m < t->month; ++m) days += month_days(t->year,m);
    days += t->day-1;
    *utc = days*86400u + t->hour*3600u + t->minute*60u + t->second - 28800u;
    return 1;
}
int phone_time_to_beijing(uint32_t utc, phone_datetime_t *t)
{
    if (!t) return 0;
    uint64_t local = (uint64_t)utc + 28800u;
    uint32_t days = (uint32_t)(local / 86400u), second = (uint32_t)(local % 86400u);
    unsigned year = 1970, month = 1;
    while (days >= (unsigned)(leap(year) ? 366 : 365)) { days -= leap(year) ? 366 : 365; ++year; }
    while (days >= month_days(year,month)) { days -= month_days(year,month); ++month; }
    t->year = (uint16_t)year; t->month = (uint8_t)month; t->day = (uint8_t)(days+1);
    t->hour = (uint8_t)(second/3600u); t->minute = (uint8_t)((second/60u)%60u);
    t->second = (uint8_t)(second%60u);
    return year >= 2000 && year <= 2099;
}
int phone_clock_set(phone_clock_t *c, uint32_t utc, uint64_t now, uint8_t source)
{
    phone_datetime_t t;
    if (!c || !phone_time_to_beijing(utc,&t)) return 0;
    c->utc = utc; c->anchor_ms = now; c->source = source; c->valid = 1;
    return 1;
}
int phone_clock_read(const phone_clock_t *c, uint64_t now, phone_datetime_t *t)
{
    if (!c || !c->valid || now < c->anchor_ms) return 0;
    uint64_t utc = (uint64_t)c->utc + (now-c->anchor_ms)/1000u;
    return utc <= UINT32_MAX && phone_time_to_beijing((uint32_t)utc,t);
}
static int digits(const char *s, unsigned n)
{
    int value = 0;
    for (unsigned i = 0; i < n; ++i) {
        if (s[i] < '0' || s[i] > '9') return -1;
        value = value*10+s[i]-'0';
    }
    return value;
}
int phone_time_parse(const char *s, uint32_t *utc)
{
    if (!s) return 0;
    size_t length=strlen(s);
    if ((length!=16 && length!=19) || s[4]!='-' || s[7]!='-' || s[10]!=' ' || s[13]!=':' ||
        (length==19 && s[16]!=':')) return 0;
    int y=digits(s,4), m=digits(s+5,2), d=digits(s+8,2), h=digits(s+11,2), min=digits(s+14,2);
    int sec=length==19 ? digits(s+17,2) : 0;
    if (y<0 || m<0 || d<0 || h<0 || min<0 || sec<0) return 0;
    phone_datetime_t t = {(uint16_t)y,(uint8_t)m,(uint8_t)d,(uint8_t)h,(uint8_t)min,(uint8_t)sec};
    return phone_time_from_beijing(&t,utc);
}
