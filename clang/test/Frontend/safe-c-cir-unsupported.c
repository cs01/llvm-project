// A construct ClangIR cannot lower is an error that says what to do, not a
// compiler crash (an atomic compound assignment crashed while reporting it).
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -fsyntax-only -Xclang -verify %s
int bump(void) {
  _Atomic int a = 0;
  a += 3; // expected-error {{Safe C cannot check this yet: its ownership check runs on ClangIR, which does not support it (atomic lvalue assign); write it another way, or move it into a small function marked '_Trusted __unsafe'}}
  return a;
}

