# Prototype: use after free over ClangIR, no annotations (2026-11-24)

Result: not good enough to build on as it stands.

- Caught 96 of 342 ASan-confirmed use-after-free and double-free programs (strictest
  level; 80 with the no-alias levels). 9 more did not lower and are not counted.
- Rejected 4 of the 143 programs tried on which ASan saw nothing.
- Rejected lines in unmodified code, strictest level: parson 17, sds 2, inih 0, and
  cmark-inlines 50, redis dict 127, zlib deflate 190. The large zlib, lua and libyaml
  counts fall to 0 to 6 per file if functions in other files free nothing, so those
  depend on cross-file knowledge. The lines were not checked by hand.
- These counts are a floor: a checker that caught more would reject more.
- Sampled misses: out parameters, stores through pointers to locals, struct
  copies, arrays of owners, library functions, atomics, `cleanup`. Most are questions of
  which pointers alias or what a callee stored, which the IR does not answer.
- ClangIR crashed on some plain corpus programs during the control run.

`bad.sh` and `real.sh` need S and D set to scratch directories.
