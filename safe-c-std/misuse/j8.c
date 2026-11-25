#include "json.h"
int main(void){ struct json j={0},k={0}; (void)json_parse(&j,STR("1")); k=j; json_free(&j); json_free(&k); return 0; }
