#include "parson.h"
#include <stdio.h>
int f(JSON_Array*a){ JSON_Array*__single _Nullable in=json_array_get_array(a,0); json_array_clear(a); return (int)json_array_get_count(in);}
