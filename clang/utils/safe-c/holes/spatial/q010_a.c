struct S{char c;long a;int*_Nullable p;};int f(struct S*);int main(void){struct S s[2]={{1,0x4141414141414141,0},{1,0x4141414141414141,0}};return f(s);}
