// RUN: %clang_cc1 -std=c++17 -triple arm64-apple-macosx14 -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -std=c++17 -triple arm64-apple-macosx14 -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -std=c++17 -triple arm64-apple-macosx14 -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM

// Darwin thread wrappers, __tls_init and the init function an unordered
// (variable template) init alias points at use the fast TLS convention and
// are nounwind, as in classic codegen.

int init();
thread_local int x = init();
template <typename T> thread_local int z = init();
int use() { return x + z<int>; }

// CIR-DAG: cir.func {{.*}}@_ZTW1x() {{.*}}cc(cxx_fast_tls) attributes {nothrow}
// CIR-DAG: cir.func {{.*}}@__tls_init() cc(cxx_fast_tls) attributes {nothrow}

// LLVM-DAG: call cxx_fast_tlscc ptr @_ZTW1x()
// LLVM-DAG: define cxx_fast_tlscc {{.*}}ptr @_ZTW1x() [[NU:#[0-9]+]]
// LLVM-DAG: call cxx_fast_tlscc void @_ZTH1x()
// LLVM-DAG: define weak_odr hidden cxx_fast_tlscc {{.*}}ptr @_ZTW1zIiE()
// LLVM-DAG: call cxx_fast_tlscc void @_ZTH1zIiE()
// LLVM-DAG: define internal cxx_fast_tlscc void @__tls_init()
// LLVM-DAG: define internal cxx_fast_tlscc void @__cxx_global_var_init.1()
// LLVM-DAG: attributes [[NU]] = { {{.*}}nounwind
