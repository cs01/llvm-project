#include "json.h"
int main(void){ struct json j={0}; (void)json_parse(&j,STR("\"abc\"")); (void)json_parse(&j,json_text(&j,0)); json_free(&j); return 0; }
