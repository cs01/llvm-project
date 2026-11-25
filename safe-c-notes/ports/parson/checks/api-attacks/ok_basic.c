#include "parson.h"
#include <stdio.h>
int f(void){ JSON_Value *_Owned __single _Nullable v=json_value_init_object(); JSON_Object *__single _Nullable o=json_value_get_object(v); json_object_set_value(o,"a",json_value_init_number(1)); double d=json_object_get_number(o,"a"); json_value_free(v); return (int)d; }
