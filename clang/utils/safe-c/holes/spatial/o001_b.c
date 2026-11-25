struct T{int a[2];};struct S{struct T*_Nullable t;};int f(struct S*s){return s->t?s->t->a[1]:0;}
