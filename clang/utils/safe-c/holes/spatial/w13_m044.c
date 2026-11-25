#include <mach/mach.h>
int main(void){long a=1;char src[8]="AAAA";vm_size_t z;vm_read_overwrite(mach_task_self(),(vm_address_t)src,4,(vm_address_t)&a,&z);return a!=1?42:0;}
