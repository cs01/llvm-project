// M5 rule 3: data guarded by a lock is reached only while holding it, a
// function that requires a lock is called only with it held, and locks are
// released on every path. Clang's thread-safety analysis, always on and an
// error under Safe C. The control run is plain C (no analysis).
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -fsyntax-only -include safec.h -verify=off %s
// off-no-diagnostics

struct _Capability("mutex") mutex { int state; };
void mutex_lock(struct mutex *m) _Acquires(m);
void mutex_unlock(struct mutex *m) _Releases(m);

struct mutex stats_lock;
int hits _Guarded_by(stats_lock);

void bump(void) _Requires(&stats_lock) { hits++; }

void good(void) {
  mutex_lock(&stats_lock);
  hits++;
  bump();
  mutex_unlock(&stats_lock);
}

int unguarded(void) {
  return hits; // expected-error {{reading variable 'hits' requires holding mutex 'stats_lock'}}
}

void no_lock_call(void) {
  bump(); // expected-error {{calling function 'bump' requires holding mutex 'stats_lock' exclusively}}
}

void forgot_unlock(int c) {
  mutex_lock(&stats_lock); // expected-note {{mutex acquired here}}
  if (c)
    return;
  mutex_unlock(&stats_lock);
} // expected-error {{mutex 'stats_lock' is not held on every path through here}}
