// M5 with the real pthread.h: a thread's argument is moved into it (owner,
// static data or null; never a borrow), the spawning code may not use what
// it handed over, and a start routine whose types differ from the SDK's only
// in sugar is accepted, as long as its parameter accepts null.
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -fsyntax-only -Xclang -verify %s
#include <pthread.h>
#include <safec.h>
#include <stdlib.h>
static _Atomic int config;
void *_Nullable work(void *_Nullable p) { (void)p; return NULL; }
void *_Nullable strict_work(void *p) { (void)p; return NULL; }

int spawns(int *borrowed) {
  pthread_t __single t;
  int local = 0;
  pthread_create(&t, 0, work, &local);  // expected-error {{the new thread may outlive what this pointer borrows}}
  pthread_create(&t, 0, work, borrowed); // expected-error {{the new thread may outlive what this pointer borrows}}
  pthread_create(&t, 0, work, &config);
  pthread_create(&t, 0, strict_work, NULL); // expected-error {{can treat memory as the wrong type}} expected-note {{}}
  int *_Nullable job = malloc(sizeof(int));
  pthread_create(&t, 0, work, job);         // expected-note {{handed to the thread here}}
  return job ? *job : 0;                    // expected-error {{this memory was handed to a new thread, which may be using it right now; using it here too is a data race; give the thread its own copy, or share it through a lock}}
}
