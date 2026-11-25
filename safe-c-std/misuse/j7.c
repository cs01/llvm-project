#include "json.h"
int main(void){ struct json j={0}; (void)json_parse(&j,STR("\"abc\"")); struct json_value v=json_at(&j,0); struct json_span b={1u<<30,1u<<30}; v.kind=JSON_TEXT; v.text=b; json_values_set(&j.values,0,v); struct str s=json_text(&j,0); int r=(int)s.len; json_free(&j); return r; }
