const char*__terminated_by('/') f(void);int main(void){const char*__terminated_by('/')p=f();int n=0;while(*p!='/'){p++;n++;}return n;}
