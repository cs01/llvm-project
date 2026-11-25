#include "text.h"
int main(void){ struct str w={"",0}; { char b[4]={'a',',','b',0}; struct str s={b,4}; w=str_before(s,','); } return w.len?w.p[0]:0; }
