Small programs with one bug each. Run any of these from the top of the repository:

  SDKROOT=$(xcrun --show-sdk-path) build.noindex/bin/clang -fsafe-c try/use_after_free.c
  SDKROOT=$(xcrun --show-sdk-path) build.noindex/bin/clang -fsafe-c try/leak.c
  SDKROOT=$(xcrun --show-sdk-path) build.noindex/bin/clang -fsafe-c try/double_free.c
  SDKROOT=$(xcrun --show-sdk-path) build.noindex/bin/clang -fsafe-c try/overflow.c -o /tmp/overflow && /tmp/overflow; echo "exit $?"

  SDKROOT=$(xcrun --show-sdk-path) build.noindex/bin/clang -fsafe-c -fsyntax-only try/struct_leaks.c

The first three are compile errors; each file shows its error, copied from the compiler, under the line it points at. struct_leaks.c has four leaks inside structs, each next to the correct version. overflow.c compiles, and stops at the bad read (exit 133) instead of reading past the array.
Edit them and rerun: fix the bug and the error goes away.
