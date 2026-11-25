#pragma pack(1)
struct S{char c;int a;};int f(struct S*);int main(void){struct S s={1,2};return f(&s);}
