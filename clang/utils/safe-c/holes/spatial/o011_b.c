typedef struct{int*_Nullable a;}T;struct S{T*_Nullable t;};int f(struct S*t){return t->t&&t->t->a?*t->t->a:0;}
