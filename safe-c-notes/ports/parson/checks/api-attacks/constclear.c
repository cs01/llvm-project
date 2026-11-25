#include "parson.h"
#include <stdio.h>
void f(const JSON_Object*o){ json_object_clear(json_value_get_object(json_object_get_value(o,"a"))); }
