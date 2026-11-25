#include "args.h"
#define VEC_NAME ints
#define VEC_TYPE int
#include "vec.h"
#define MAP_NAME cnt
#define MAP_VALUE int
#include "map.h"
static const char*_Nullable f(struct str s){ return s.len?&s.p[0]:NULL; }
int main(void){ const char*_Nullable p=NULL; for(int i=0;i<2;i++){ char b[2]={1,2}; if(i&&p) return *p; p=f((struct str){b,2}); } return 0; }
