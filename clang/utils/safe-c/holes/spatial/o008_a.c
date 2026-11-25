struct T{long a;};int f(int(*)(struct T*));int g(struct T*t){return t->a==0;}int main(void){return f(g);}
