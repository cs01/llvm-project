# Five Unix tools in Safe C

`wc`, `uniq`, `comm`, `sort` and `tsort` from suckless [sbase](https://core.suckless.org/sbase/) (MIT, see LICENSE), rewritten with `text.h`, `vec.h` and `args.h`.
No `__unsafe`, no `_Trusted`, no ownership marks in `sort`'s or `tsort`'s data: lines, vertices and edges are indexes. They are also C99 for other compilers.

`diff.ts` compares them with the originals on random ASCII input. They have fewer options (no UTF-8, `sort` only `-nru`, `-t`, `-k column`).
