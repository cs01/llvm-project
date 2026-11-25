#include "json.h"
int main(void){ struct json j={0}; json_parse(&j,STR("[1,2]")); double d=json_number(&j,99999)+(double)json_text(&j,77777).len+(double)json_first(&j,88888); json_free(&j); return d!=0; }
