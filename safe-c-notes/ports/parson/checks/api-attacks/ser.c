#include "parson.h"
#include <stdio.h>
int f(JSON_Value*v){ char *_Owned _Nullable __null_terminated s=json_serialize_to_string(v); json_free_serialized_string(s); return s?*s:0;}
