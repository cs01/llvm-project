#include <mach/mach.h>
int main(void){int x=1;int*_Nullable p=&x;long v=0x4141414141;vm_size_t z;vm_read_overwrite(mach_task_self(),(vm_address_t)&v,8,(vm_address_t)&p,&z);return p?*p:0;}
