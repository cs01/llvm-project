#include "parson.h"
#include <stdio.h>
int f(JSON_Object*o,JSON_Object*p){ JSON_Value *_Owned __single _Nullable v=json_value_init_null(); json_object_set_value(o,"a",v); json_object_set_value(p,"a",v); return 0;}
