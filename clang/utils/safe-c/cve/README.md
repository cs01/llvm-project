# Replaying real bugs (2026-11-25)

Magma v1.2 is a fuzzing benchmark of real bugs in real C libraries, each as a patch. From its 80 patches for libpng, libtiff, libsndfile, lua,
sqlite3 and libxml2, the first 20 by sha1 of the patch's path, fixed before any was read. 3 are not memory bugs (XML003 reads external files,
TIF014 uses too much memory, SQL012 is a logic error).

The other 17: **3 rejected at compile time** (PNG007 and TIF004, null; XML002, type confusion), **14 stopped at run time**, each with a good input
that runs. 0 missed. `run.sh` checks it.

What this does not show:
- They are hand-written reductions, a few lines each, not the libraries. Whoever wrote them knew the bug.
- Buffers are marked with the size the original allocates. A port that marks the wrong size is not protected by it.
- None of the 17 is a use after free or double free, so it says nothing about ownership.
- Stopped means the program traps: an attacker gets a crash, not memory.

## Use after free

`uaf.sh`: the 4 bugs whose CVE is described as a use after free, among the 110 of Magma's bugs with CVE numbers in the C libraries (libpng, libtiff,
libsndfile, libxml2, lua, sqlite3, openssl, php). PNG002 (CVE-2019-7317), SSL002 (CVE-2016-6309, two ways), XML012 (CVE-2016-1836), XML013
(CVE-2016-1837). **4 of 4 rejected at compile time**, and the fixed version of each is accepted and runs. PNG002 holds the freed pointer in a local, where
libpng reads it again from the struct. The same limits as above: reductions by hand.
