#include "json.h"
int main(void){ struct json j = {0}; json_result r = json_parse(&j, STR("[1,")); size_t n = r.value; json_free(&j); return (int)n; }
