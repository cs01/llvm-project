// Fuzzing hole 10 (2026-11-23), M5: a signal handler can interrupt any
// code, so it may only touch globals that are _Atomic, volatile sig_atomic_t
// or const (C11 7.14.1.1p5), also through the functions it calls. Checked
// for signal() and sigaction's handler fields. The control run is plain C.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -fsyntax-only -include safec.h -verify=off %s
// off-no-diagnostics

typedef void (*handler_t)(int);
handler_t _Nullable signal(int, handler_t _Nullable);
struct sigaction { handler_t _Nullable sa_handler; int sa_flags; };
void free(void *_Nullable);
typedef int sig_atomic_t;

static int *_Owned _Nullable g;
static int counter;
static volatile sig_atomic_t stop;
static _Atomic int hits;
static const int limit = 3;

static void frees(int s) { (void)s; free(_Take(g)); } // expected-note {{'g' is used here}}
static void bump(void) { counter++; }                 // expected-note {{'counter' is used here}}
static void indirect(int s) { (void)s; bump(); }
static void ok(int s) { (void)s; stop = 1; hits++; (void)limit; }

void install(void) {
  signal(2, frees); // expected-error {{'frees' is a signal handler, so it can run in the middle of any code, including code that is using 'g'; a handler may only touch globals that are '_Atomic', 'volatile sig_atomic_t' or 'const' (set a flag here and do the work in the main loop)}}
  struct sigaction sa = {0};
  sa.sa_handler = indirect; // expected-error {{'indirect' is a signal handler}}
  signal(15, ok);
}
