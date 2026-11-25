struct T{int*_Nullable a;};int f(struct T(*s)[1]){return (*s)[0].a?*(*s)[0].a:0;}
