#include "parson.h"
#include <stdio.h>
int f(JSON_Object*o){ JSON_Value*__single _Nullable s=json_object_get_value(o,"a"); json_object_set_null(o,"a"); return json_value_get_type(s);}
