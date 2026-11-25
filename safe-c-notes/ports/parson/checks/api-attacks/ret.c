#include "parson.h"
#include <stdio.h>
JSON_Value*_Nullable f(void){ JSON_Value *_Owned __single _Nullable v=json_parse_string("1"); return json_array_get_value(json_value_get_array(v),0);}
