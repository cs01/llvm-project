struct S;int f(struct S*);int main(void){long a=0x41414141;return f((struct S*)&a);}
