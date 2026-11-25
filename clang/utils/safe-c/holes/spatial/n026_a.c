int g(int(*)(int*__counted_by(3)));int f(int*__counted_by(3)p){return p[2];}int main(void){return g(f);}
