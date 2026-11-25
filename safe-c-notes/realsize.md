# Five real files under hardened C, ownership off (2026-11-24)

redis adlist, cmark utf8, zlib inftrees, lua lstring, BearSSL sha2small; ported and classified by an agent, outputs compared with
pristine at -O0 and -O2 (rerun by the maintainer of this note after the subscript fix: same).
Percent columns are of the file's lines, computed from the agent's counts.

| file | lines | errors | annotation | small edit | restructure | `__unsafe` | our gap, % of lines | time |
|---|---|---|---|---|---|---|---|---|
| redis adlist | 395 | 33 | 35 | 11 | 0 | 5 | 1.5 | 0.99 |
| cmark utf8 | 428 | 29 | 12 | 3 | 0 | 2 | 1.2 | 1.05 to 1.08 |
| zlib inftrees | 424 | 15 | 4 | 15 | 0 | 2 | 0.2 | 1.28 |
| lua lstring | 353 | 45 | 22 | 42 | 0 | 20 | 4.2 | 1.06 |
| BearSSL sha2small | 341 | 49 | 23 | 1 | 21 | 8 | 6.7 | 1.14 |

Times were measured before `a[i]++` was checked; zlib's uses it, so it is a floor. Bar set before: our gap plus
restructuring under 5% of lines: 4 of 5 (BearSSL is over, for one missing feature: an unsafe cast in a global initializer).
Would the maintainer accept it, in the agent's judgment: redis mostly, cmark yes, zlib no (speed), Lua no (20 `__unsafe`),
BearSSL after that feature. No pristine bug was found. Not covered: libyaml's scanner (26 errors per 100 lines), Linux.

Our gaps: casts in global initializers; plain C callees need the legacy pragma to link (adoption.md says otherwise, wrongly);
allocators in legacy headers rejected as not zeroed; `void *` locals needing `__single`, `bsearch`; one past the end stored to `__single`
traps on legal input; `__unsafe` on a global silently does nothing.
