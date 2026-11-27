// Under -fsafe-c, malloc(n) is emitted as calloc(1, n): an uninitialized
// pointer in heap memory would be a wild pointer, a zeroed one is null and
// traps on use. -fno-builtin does not turn this off.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -o - -fsafe-c %s \
// RUN:   | FileCheck %s --check-prefix=SAFEC
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -o - -fsafe-c \
// RUN:   -fno-builtin %s | FileCheck %s --check-prefix=SAFEC
// RUN: %clang_cc1 -triple arm64-apple-macosx14.0 -emit-llvm -o - -fsafe-c %s \
// RUN:   | FileCheck %s --check-prefix=DARWIN
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefix=PLAIN

typedef __SIZE_TYPE__ size_t;
void *malloc(size_t);
void *realloc(void *, size_t);

struct node { int v; long next; };

__attribute__((ownership_returns(malloc))) struct node *make(void) { return malloc(sizeof(struct node)); }

// SAFEC-LABEL: define {{.*}} @make(
// SAFEC: call noalias ptr @calloc(i64 1, i64 16)
// SAFEC-NOT: @malloc
// PLAIN-LABEL: define {{.*}} @make(
// PLAIN: call {{.*}}ptr @malloc(i64 {{.*}}16)

// realloc(NULL, n) allocates like malloc(n): zeroed the same way. A realloc of a
// real pointer stays a realloc.
__attribute__((ownership_returns(malloc))) struct node *make2(void) { return realloc(0, sizeof(struct node)); }

// SAFEC-LABEL: define {{.*}} @make2(
// SAFEC: call noalias ptr @calloc(i64 1, i64 16)
// SAFEC-NOT: @realloc
// PLAIN-LABEL: define {{.*}} @make2(
// PLAIN: call {{.*}}ptr @realloc(ptr {{.*}}null, i64 {{.*}}16)

// realloc(p, n) zeroes the bytes it adds, from the allocator's size of the old
// block (not p's bounds, which can be smaller than the block).
__attribute__((ownership_returns(malloc))) struct node *grow(struct node *p) { return realloc(p, 4 * sizeof(struct node)); }

// SAFEC-LABEL: define {{.*}} @grow(
// SAFEC: call i64 @malloc_usable_size(ptr
// SAFEC: %[[OLD:.*]] = phi i64
// SAFEC: %[[NEW:.*]] = call ptr @realloc(ptr {{.*}}, i64 64)
// SAFEC: icmp ugt i64 64, %[[OLD]]
// SAFEC: call void @llvm.memset
// PLAIN-LABEL: define {{.*}} @grow(
// PLAIN-NOT: malloc_usable_size
// DARWIN-LABEL: define {{.*}} @grow(
// DARWIN: call i64 @malloc_size(ptr
