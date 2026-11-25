#include "parson.h"
#include <stdio.h>
void f(JSON_Object*o){ json_value_free(json_object_get_wrapping_value(o)); }
