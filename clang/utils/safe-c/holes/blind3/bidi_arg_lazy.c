#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
struct node { int v; struct node *_Owned _Nullable next; };
struct list { struct node *_Owned _Nullable head; };
static struct node *_Owned mk(int v) { struct node *_Owned _Nullable n = calloc(1, sizeof *n); if (!n) exit(1); n->v = v; return n; }
static int *_Owned mki(int v) { int *_Owned _Nullable n = calloc(1, sizeof *n); if (!n) exit(1); *n = v; return n; }
static void drop(struct node *_Owned n) { free(n); }
static void clear(struct list *l) { while (l->head) { struct node *_Owned _Nullable n = _Take(l->head); if (!n) return; l->head = _Take(n->next); free(n); } }
static void use(int x) { printf("%d\n", x); }
struct X{int*_Static _Nullable __bidi_indexable p;}; static int f(int*_Nullable __bidi_indexable a,int k){return a?a[0]+k:0;} static int gg;
static int kill(struct X*_Owned x){free(x);return 1;}
int main(){struct X*_Owned _Nullable x=calloc(1,sizeof*x); if(!x)return 1; x->p=&gg; use(f(x->p,kill(x)));}
