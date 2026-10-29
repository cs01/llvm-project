// REQUIRES: aarch64-registered-target

// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +crc -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +crc -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +crc -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

// ldexp and the AArch64 CRC32 builtins: one LLVM intrinsic each, the same
// through ClangIR as through classic codegen.

float scale_f(float x, int e) { return __builtin_ldexpf(x, e); }
// CIR-LABEL: @scale_f
// CIR: cir.call_llvm_intrinsic "ldexp" {{.*}} : (!cir.float, !s32i) -> !cir.float
// LLVM-LABEL: @scale_f
// LLVM: call float @llvm.ldexp.f32.i32(float {{.*}}, i32 {{.*}})

double scale(double x, int e) { return __builtin_ldexp(x, e); }
// LLVM-LABEL: @scale(
// LLVM: call double @llvm.ldexp.f64.i32(double {{.*}}, i32 {{.*}})

unsigned crc_byte(unsigned c, unsigned char b) { return __builtin_arm_crc32cb(c, b); }
// CIR-LABEL: @crc_byte
// CIR: cir.call_llvm_intrinsic "aarch64.crc32cb"
// LLVM-LABEL: @crc_byte
// LLVM: [[B:%.*]] = zext i8 {{.*}} to i32
// LLVM: call i32 @llvm.aarch64.crc32cb(i32 {{.*}}, i32 [[B]])

unsigned crc_word(unsigned c, unsigned long long d) { return __builtin_arm_crc32cd(c, d); }
// LLVM-LABEL: @crc_word
// LLVM: call i32 @llvm.aarch64.crc32cx(i32 {{.*}}, i64 {{.*}})
