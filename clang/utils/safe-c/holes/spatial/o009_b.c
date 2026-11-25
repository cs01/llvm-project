struct T{int*_Nullable a;};int f(struct T(*g)(void)){struct T t=g();return t.a?*t.a:0;}
