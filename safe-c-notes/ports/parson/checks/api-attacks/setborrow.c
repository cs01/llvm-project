#include "parson.h"
#include <stdio.h>
int f(JSON_Object*o,JSON_Object*p){ return json_object_set_value(o,"a",json_object_get_value(p,"b"));}
