#include "map.h"
#include "args.h"
VEC(ints,int);
MAP(cnt,int);
int main(void){ struct cnt m={0}; cnt_put(&m,STR("a"),1); m.len=0; m.filled=0; for(int i=0;i<3;i++) cnt_put(&m,STR("q"),i); text_free(&m.keys); int *_Nullable p=cnt_get(&m,STR("a")); int r=p?*p:0; cnt_free(&m); return r; }
