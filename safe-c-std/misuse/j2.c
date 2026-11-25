#include "json.h"
int main(void){ struct json j={0}; json_parse(&j,STR("\"abc\"")); struct str s=json_text(&j,0); json_free(&j); return s.len?s.p[0]:0; }
