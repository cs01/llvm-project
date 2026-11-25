#include "parson.h"
#include <stdio.h>
int g(const JSON_Value*v){ JSON_Object*__single _Nullable o=json_value_get_object(v); json_object_clear(o); return 0;} int f(JSON_Value*v){ JSON_Value*__single _Nullable c=json_object_get_value(json_value_get_object(v),"a"); g(v); return json_value_get_type(c);}
