#include "json.h"
int main(void){ struct json j={0}; json_parse(&j,STR("[1]")); struct json_value v=json_at(&j,0); return (int)v.number; }
