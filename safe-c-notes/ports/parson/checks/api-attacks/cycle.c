#include "parson.h"
#include <stdio.h>
int f(void){ JSON_Value *_Owned __single _Nullable v=json_value_init_array(); json_array_append_value(json_value_get_array(v), v); return 0;}
