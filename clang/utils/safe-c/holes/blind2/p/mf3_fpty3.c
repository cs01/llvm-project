// opts: +mf3_fpty32
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
struct o{ int(*f)(int*__counted_by(n),int n); }; void g(struct o*o); static int pick(int*__counted_by(n) a,int n){ return a[n-1];} int main(void){ struct o o={pick}; g(&o); }
