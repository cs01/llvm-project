#include "parson.h"
#include <stdio.h>
int f(void){ JSON_Value *_Owned __single _Nullable v=json_parse_string("[1]"); JSON_Array *__single _Nullable a=json_value_get_array(v); const JSON_Value *__single _Nullable x=json_array_get_value(a,0); int p=json_value_get_parent(x)==v; json_value_free(v); return p; }
