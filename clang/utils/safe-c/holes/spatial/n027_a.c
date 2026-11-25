struct O{int(*c)(int*__counted_by(3));};int g(struct O*);int f(int*__counted_by(3)p){return p[2];}int main(void){struct O o={f};return g(&o);}
