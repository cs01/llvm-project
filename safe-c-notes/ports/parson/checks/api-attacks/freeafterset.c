#include "parson.h"
#include <stdio.h>
int f(JSON_Object*o){ JSON_Value *_Owned __single _Nullable v=json_value_init_null(); if(json_object_set_value(o,"a",v)!=JSONSuccess) json_value_free(v); return 0;}
