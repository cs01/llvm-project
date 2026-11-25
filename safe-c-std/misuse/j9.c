#include "json.h"
int main(void){ struct json j={0}; (void)json_parse(&j,STR("[1]")); struct json_value v=json_at(&j,0); if(v.kind==JSON_NUMBER) return (int)v.first; return 0; }
