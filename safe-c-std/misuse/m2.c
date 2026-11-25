#include "args.h"
#define VEC_NAME ints
#define VEC_TYPE int
#include "vec.h"
#define MAP_NAME cnt
#define MAP_VALUE int
#include "map.h"
int main(void){ struct cnt m={0}; cnt_put(&m,STR("a"),1); struct str k=text_str(&m.keys); cnt_put(&m,k,2); cnt_free(&m); return 0; }
