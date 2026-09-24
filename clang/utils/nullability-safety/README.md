# Corpus measurements for -fnullability-safety

`corpus.py` runs the analysis over real public code (`corpus.json`) to measure
false positives and false negatives. The lit tests only cover the cases someone
thought to write down. This tool shows what the analysis does on code nobody
wrote for it.

## Corpus

| project | lang | what it is |
|---|---|---|
| llvm-support | C++ | `llvm/lib/Support` from this checkout, built from `build/compile_commands.json` |
| libuv | C | libuv event loop, pinned commit |
| bdwgc | C | Boehm GC, pinned commit |
| cbmc-util | C++ | `src/util` of CBMC, from its own `compile_commands.json` |

External projects are expected as sibling checkouts of this repository
(`--corpus-root` changes that). The tool warns when a checkout is not at the
pinned commit, because the numbers are then not comparable.

## Checking a change for new false positives

Save the compiler before the change, rebuild, and compare:

```
cp build/bin/clang-24 /tmp/clang-base
ninja -C build clang
clang/utils/nullability-safety/corpus.py --mode nonnull compare \
    --old-clang /tmp/clang-base --new-clang build/bin/clang
clang/utils/nullability-safety/corpus.py --mode nullable compare \
    --old-clang /tmp/clang-base --new-clang build/bin/clang
```

Warnings are matched on file, diagnostic, message and source line text, so an
unchanged warning at a shifted line is not a change. Every added warning needs
a look before the change lands. The command exits 1 when warnings were added.

`nonnull` mode is closest to how the analysis is adopted: unannotated pointers
are trusted, so each warning in unannotated code comes from a real nullable
source (a null initializer, `malloc`, `reset()`, ...) and is either a bug or a
false positive. `nullable` mode treats every unannotated pointer as nullable
and exercises the flow engine much harder.

## Measuring missed bugs

```
clang/utils/nullability-safety/corpus.py mutate --clang build/bin/clang --sample 300
```

This finds real guards on pointer variables (`if (!p) return;`, `if (p == NULL)
continue;`, ...) that are followed by a dereference of `p`, deletes one guard at
a time without moving any lines, and checks that the analysis now warns in the
guarded region. It reports the share caught and lists every miss. Guards in
code the preprocessor drops are skipped. A miss can still be a harness artifact
(a shadowed variable, say), so check it before filing it as a gap.

The mutants only cover the simple "check, then dereference" shape, so a high
figure says the core flow engine works on real code. It says nothing about
aliasing, cross-function flow, or other harder cases.
