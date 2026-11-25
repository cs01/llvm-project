struct S{int*_Nullable a;};int f(struct S*p){return p->a?*p->a:0;}
