#include "map.h"
#include "args.h"
VEC(ints,int);
MAP(cnt,int);
int main(void){ struct cnt m={0}; cnt_put(&m,STR("a"),1); struct str k=text_str(&m.keys); cnt_put(&m,k,2); cnt_free(&m); return 0; }
