# Everyday C: what each rejection costs (2026-11-24)

18 ordinary programs (second attack's false rejections) before, and after the smallest change found. Default mode.
2 compile unchanged, 15 compile and run after changing 1 or 2 lines (plus a wrapper line in 3), 1 has no spelling found
(`r_mutex`: a lock wrapper for `_Guarded_by`). Written by someone who knows the compiler; a newcomer would be slower.

- Mark or reorder only: asprintf, strtol, stridx (pointer walk), pushback (null check).
- Different function or data shape, behavior not identical: getline to fgets (long lines split), sigaction to signal,
  strtok_r to a hand loop (tokens cut at 7), `double a[n][n]` to a flat array, `**root` insert to a loop on nodes.
- One `__unsafe` line: ctx, qsort, thread_arg (`void *` back to its type), and wrappers for readdir, bind, setenv
  (a library header without annotations; one wrapper per function, reusable).
