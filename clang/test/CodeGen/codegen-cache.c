// REQUIRES: x86-registered-target
// RUN: rm -rf %t && mkdir %t
// RUN: %clang_cc1 -triple x86_64-linux-gnu -emit-obj -fcodegen-cache-path=%t/cache %s -o %t/a.o
// RUN: ls %t/cache/*/*.o | count 1

// The same compile again reuses the entry and writes the same object.
// RUN: %clang_cc1 -triple x86_64-linux-gnu -emit-obj -fcodegen-cache-path=%t/cache %s -o %t/b.o
// RUN: cmp %t/a.o %t/b.o
// RUN: ls %t/cache/*/*.o | count 1

// A hit copies the entry without running the backend: replace the entry and
// the next compile writes the replacement.
// RUN: %python -c "import glob, sys; [open(p, 'w').write('from the cache') for p in glob.glob(sys.argv[1])]" '%t/cache/*/*.o'
// RUN: %clang_cc1 -triple x86_64-linux-gnu -emit-obj -fcodegen-cache-path=%t/cache %s -o %t/c.o
// RUN: FileCheck %s --check-prefix=HIT --input-file=%t/c.o
// HIT: from the cache

// Different IR, or different options, is a different key.
// RUN: %clang_cc1 -triple x86_64-linux-gnu -emit-obj -fcodegen-cache-path=%t/cache %s -o %t/d.o -DCHANGED
// RUN: ls %t/cache/*/*.o | count 2
// RUN: %clang_cc1 -triple x86_64-linux-gnu -emit-obj -fcodegen-cache-path=%t/cache %s -o %t/e.o -O2
// RUN: ls %t/cache/*/*.o | count 3

// The output file name is not part of the key.
// RUN: %clang_cc1 -triple x86_64-linux-gnu -emit-obj -fcodegen-cache-path=%t/cache %s -o %t/elsewhere.o -DCHANGED
// RUN: ls %t/cache/*/*.o | count 3

// Only object output is cached.
// RUN: %clang_cc1 -triple x86_64-linux-gnu -S -fcodegen-cache-path=%t/cache %s -o %t/f.s -DOTHER
// RUN: ls %t/cache/*/*.o | count 3

// A compile whose backend warns is not stored, since a hit could not repeat
// the warning.
// RUN: %clang_cc1 -triple x86_64-linux-gnu -emit-obj -fcodegen-cache-path=%t/cache %s -o %t/g.o -DWARNS -fwarn-stack-size=100 -verify=warns
// RUN: ls %t/cache/*/*.o | count 3

int f(void) {
#ifdef CHANGED
  return 2;
#else
  return 1;
#endif
}

#ifdef WARNS
// warns-warning@+1 {{stack frame size}}
void big(void) {
  volatile char buf[4096];
  buf[0] = 0;
}
#endif
