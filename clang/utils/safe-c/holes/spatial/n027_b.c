struct O{int(*c)(int*);};int g(struct O*o){int a=1;return o->c(&a);}
