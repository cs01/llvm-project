// opts: +mf2_exclfp2
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
struct s{int*_Owned _Nullable p;}; typedef void(*cb)(struct s*); cb get(void); int main(void){ struct s a={0}; a.p=malloc(4); int*_Nullable q=a.p; get()(&a); if(q) printf("%d\n",*q); }
