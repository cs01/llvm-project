// M5 rule 2 (Rust's `static` must be `Sync`): a mutable global is shared by
// every thread, so it must be _Thread_local, _Atomic, guarded by a lock, or
// const. A file-static global its file never writes is a constant in all but
// name and needs nothing. The control run is plain C.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -fsyntax-only -include safec.h -verify=off %s
// off-no-diagnostics

typedef int sig_atomic_t;
struct _Capability("mutex") mtx { int state; };
struct mtx lock;
void take(int *p);
void show(const char *s);
void fillc(char *p);

int counter; // expected-error {{'counter' is a mutable global, so every thread shares it and two threads can write it at once; make it '_Thread_local' (one copy per thread, the usual fix for single-threaded code), '_Atomic', guarded by a lock ('_Guarded_by(m)'), or 'const' if it never changes; if it is written only before any thread starts, mark it '_Unsynchronized'}}
static int hits; // expected-error {{'hits' is a mutable global}}
static int lent; // expected-error {{'lent' is a mutable global}}
static char buf[8]; // expected-error {{'buf' is a mutable global}}

_Thread_local int per_thread;
_Atomic int shared_count;
int guarded _Guarded_by(lock);
const int limit = 10;
static const char name[] = "x";
static int staticTrue = 1; // never written: a constant
static int table[4] = {1, 2, 3, 4}; // indexed, never written
static volatile sig_atomic_t stop;

void bump(void) {
  hits++;       // expected-note {{'hits' is written here}}
  take(&lent);  // expected-note {{'lent' is written here}}
  fillc(buf);   // expected-note {{'buf' is written here}}
  per_thread++;
  shared_count++;
  stop = 1;
  show(name);
  if (staticTrue)
    (void)table[2];
}

void f(void) {
  static int calls; // expected-error {{'calls' is a mutable global}}
  calls++;          // expected-note {{'calls' is written here}}
}

// Written only before threads start (hooks, settings): Rust's static mut.
static int log_level _Unsynchronized;
void setup(void) { __unsafe { log_level = 2; } }
void later(void) {
  log_level = 3; // expected-error {{'log_level' is '_Unsynchronized': it is shared without a lock, so a write is safe only before any thread starts; put this write inside '__unsafe { }' to say it is one of those}}
}
