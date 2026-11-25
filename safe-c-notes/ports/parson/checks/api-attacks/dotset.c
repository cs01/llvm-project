#include "parson.h"
#include <stdio.h>
int f(JSON_Object*o){ JSON_Object*__single _Nullable in=json_object_get_object(o,"a"); json_object_dotset_null(o,"a.b"); json_object_dotremove(o,"a"); return (int)json_object_get_count(in);}
