#include "json.h"
int main(void){ struct json j={0}; (void)json_parse(&j,STR("\"abc\"")); struct json_value v=json_at(&j,0); v.kind=JSON_NUMBER; json_values_set(&j.values,0,v); json_free(&j); return 0; }
