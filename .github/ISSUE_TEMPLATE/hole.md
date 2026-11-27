---
name: Hole
about: A program -fsafe-c accepts that has a memory error
labels: hole
---

**Program** (as small as you can make it):

```c
```

**Compiler:** commit or release, and the exact command (`clang -fsafe-c ...`).

**What -fsafe-c does:** accepts it, and the Safe C build runs without trapping (paste its output and exit status).

**ASan output** for the same program built with `/usr/bin/clang -fsanitize=address,undefined -include safe-c-std/safec.h`:

```
```

Programs using `__unsafe`, `_Trusted`, `#pragma clang safe_c legacy`, inline assembly, `setjmp`, threads or signals are out of scope.
