#!/usr/bin/env python3
# Generates a pointer-dense C TU: worst case for the flow-sensitive nullability
# pass (many derefs, guards, ternaries, loops). Usage: gen_synth.py [N] > out.c
import sys
N = int(sys.argv[1]) if len(sys.argv) > 1 else 2500
out = ['// auto-generated pointer-heavy TU (worst case for nullability flow analysis)']
out.append('typedef struct Node { int v; struct Node * _Nullable next; } Node;')
out.append('int g_(int * _Nullable, int * _Nullable);')
for i in range(N):
    out.append(f'''int f{i}(int * _Nullable a, int * _Nullable b, Node * _Nullable n) {{
    int s = 0;
    if (a) {{ s += *a; }}
    if (b) {{ s += *b; if (a) s += *a * *b; }}
    while (n) {{ s += n->v; n = n->next; }}
    int * _Nullable c = (s & 1) ? a : b;
    if (c) s += *c;
    return g_(a, b) + s;
}}''')
sys.stdout.write('\n'.join(out) + '\n')
