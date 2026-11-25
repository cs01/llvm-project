#include "parson.h"
#include <stdio.h>
int f(JSON_Object*o){ const char*_Nullable s=json_object_get_string(o,"a"); json_object_set_string(o,"a",s); return 0;}
