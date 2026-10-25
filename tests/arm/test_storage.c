#include "common.h"
#include "../../firmware/app/storage_logic.c"

void test_main(void)
{
    uint64_t free_bytes=0,total_bytes=0;
    CHECK(storage_parse_space("+QFLDS: 31266013184,31360876544\r",&free_bytes,&total_bytes));
    CHECK(free_bytes==31266013184ull && total_bytes==31360876544ull);
    CHECK(!storage_parse_space("+QFLDS: 31360876545,31360876544",&free_bytes,&total_bytes));
    CHECK(!storage_parse_space("+QFLDS: 18446744073709551616,31360876544",&free_bytes,&total_bytes));
    CHECK(!storage_parse_space("+QFLDS: -1,100",&free_bytes,&total_bytes));
    CHECK(!storage_parse_space("+QFLDS: 1,2,3",&free_bytes,&total_bytes));
    int handle=-1;uint32_t position=0,written=0;
    CHECK(storage_parse_handle("+QFOPEN: 20\r",&handle) && handle==20);
    CHECK(!storage_parse_handle("+QFOPEN: -1",&handle));
    CHECK(storage_parse_position("+QFPOSITION: 8388608",&position) && position==8388608u);
    CHECK(!storage_parse_position("+QFPOSITION: 4294967296",&position));
    CHECK(storage_parse_write("+QFWRITE: 120,8388728\r",&written) && written==120u);
    CHECK(!storage_parse_write("+QFWRITE: 121,120",&written));
    char record[256];
    storage_record_t fields={.uptime_ms=4294967296ull,.light_raw=1009,
        .input_raw=4064,.x_mg=-8,.y_mg=12,.z_mg=1001,.rsrp_dbm=-74};
    CHECK(storage_format_record(record,sizeof record,&fields)>0);
    CHECK(!strcmp(record,"\n{\"schema\":1,\"uptime_ms\":4294967296,\"ldr\":1009,\"joy\":4064,\"x_mg\":-8,\"y_mg\":12,\"z_mg\":1001,\"lte_rsrp\":-74}\n"));
    CHECK(!storage_format_record(record,8,&fields));
    test_finish(0);
}
