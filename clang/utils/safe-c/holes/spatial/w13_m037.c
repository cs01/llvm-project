#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
int main(void){struct{char b[4];char c[400];}s={{0},{7}};fcntl(0,F_GETPATH,s.b);return s.c[0]!=7?42:0;}
