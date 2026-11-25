struct S{const char*__terminated_by('/') p;};int f(struct S*s){int n=0;const char*__terminated_by('/')p=s->p;while(*p!='/'){p++;n++;}return n;}
