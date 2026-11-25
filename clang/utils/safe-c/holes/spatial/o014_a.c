#pragma pack(1)
struct S{char c;long a;long b;};int f(struct S*);int main(void){struct S s[1]={{1,1,1}};return f(s);}
