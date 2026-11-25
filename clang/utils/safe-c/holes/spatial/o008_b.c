struct T{char a;};int f(int(*g)(struct T*)){struct T t={1};return g(&t);}
