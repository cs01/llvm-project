struct T{long a;};int f(struct T(*)(void));struct T g(void){struct T t={0x41414141};return t;}int main(void){return f(g);}
