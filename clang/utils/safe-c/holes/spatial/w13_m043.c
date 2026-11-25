#include <mach/mach.h>
int main(void){struct{int a;int k[40];}q={0,{77}};mach_msg_type_number_t k=MACH_TASK_BASIC_INFO_COUNT;task_info(mach_task_self(),MACH_TASK_BASIC_INFO,&q.a,&k);return q.k[0]!=77?42:0;}
