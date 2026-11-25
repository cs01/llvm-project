#include "parson.h"
#include <stdio.h>
int f(JSON_Object*o){ const char*_Nullable k=json_object_get_name(o,0); if(k) json_object_remove(o,k); return 0;}
