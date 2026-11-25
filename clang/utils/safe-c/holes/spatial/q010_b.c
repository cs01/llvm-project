#pragma pack(4)
struct S{char c;long a;int*_Nullable p;};int f(struct S*s){return s->p?*s->p:0;}
