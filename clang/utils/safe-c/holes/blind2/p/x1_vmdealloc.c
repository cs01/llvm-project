// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <mach/mach.h>
int main(int c,char**v){ int*p=malloc(1<<20); if(!p) return 1; vm_deallocate(mach_task_self(),(vm_address_t)(uintptr_t)p,1<<20); p[5]=1; printf("%d\n",p[5]); }
