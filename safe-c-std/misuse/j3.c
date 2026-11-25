#include "json.h"
int main(void){ struct json j={0}; (void)json_parse(&j,STR("\"abc\"")); struct str s=json_text(&j,0); (void)json_parse(&j,STR("\"dddddddddddddddddddddd\"")); return s.len?s.p[0]:0; }
