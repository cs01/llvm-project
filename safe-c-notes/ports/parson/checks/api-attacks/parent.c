#include "parson.h"
#include <stdio.h>
int f(JSON_Object*o){ JSON_Value*__single _Nullable v=json_object_get_value(o,"a"); json_object_clear(json_value_get_object(json_value_get_parent(v))); return json_value_get_type(v);}
