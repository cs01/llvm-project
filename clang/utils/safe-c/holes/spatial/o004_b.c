struct T{int*_Nullable a;};struct S{struct T*_Nullable t;};int f(struct S*s){return s->t&&s->t->a?*s->t->a:0;}
