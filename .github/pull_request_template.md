**What this changes, and why:**

**Gates run** (paste the lines from `clang/utils/safe-c/check-all.sh`):

```
```

**If it rejects more code:** the new lit test, run on the compiler before this change (it must fail there):

**If it accepts more code:** why that is sound (Lean proof, or the argument), and the `corpus` output.

- [ ] No expected test output changed in the same commit as the checker, or each change is explained above
- [ ] `corpus/verdicts.txt` and `corpus/accepted.txt` not edited by hand
