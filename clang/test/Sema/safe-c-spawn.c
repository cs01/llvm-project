// M5 rule 1: a thread's argument is moved into it (Rust's 'static + Send
// bound on spawn): an owner, static data or null; never a borrow, which the
// spawning function could free or let go out of scope first. The control
// run pins the ownership rules off.
// RUN: %clang_cc1 -fsyntax-only -mllvm -safe-c-shared-globals=false -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -mllvm -safe-c-ownership=false -fsyntax-only -mllvm -safe-c-shared-globals=false -fsafe-c -fbounds-safety \
// RUN:   -fnullability-safety -fnullability-default=nonnull -include safec.h -verify=off %s
// off-no-diagnostics

typedef __SIZE_TYPE__ size_t;
typedef unsigned long pthread_t; // as on Linux
int pthread_create(pthread_t *t, const void *_Nullable attr,
                   void *_Nullable (*fn)(void *_Nullable),
                   void *_Nullable arg);
void *_Nullable malloc(size_t);
void free(void *_Nullable);
void *_Nullable work(void *_Nullable p);
static int config;

void spawns(int *borrowed) {
  pthread_t t;
  int local = 0;
  pthread_create(&t, 0, work, &local);  // expected-error {{the new thread may outlive what this pointer borrows (the spawning function can return or free it first); give the thread memory it owns (a new allocation, moved in) or static data}}
  pthread_create(&t, 0, work, borrowed); // expected-error {{the new thread may outlive what this pointer borrows}}
  int *_Nullable job = malloc(sizeof(int));
  pthread_create(&t, 0, work, job);     // moved in
  pthread_create(&t, 0, work, &config); // static data
  pthread_create(&t, 0, work, 0);
}
