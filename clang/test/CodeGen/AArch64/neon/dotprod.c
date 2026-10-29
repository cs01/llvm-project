// REQUIRES: aarch64-registered-target || arm-registered-target

// RUN:                   %clang_cc1_cg_arm64_neon           -target-feature +dotprod -emit-llvm %s -disable-O0-optnone | opt -S -passes=mem2reg,sroa,instcombine | FileCheck %s --check-prefix=LLVM
// RUN: %if cir-enabled %{%clang_cc1_cg_arm64_neon -target-feature +dotprod -fclangir -emit-llvm %s -disable-O0-optnone | opt -S -passes=mem2reg,sroa,instcombine | FileCheck %s --check-prefix=LLVM %}

#include <arm_neon.h>

// LLVM-LABEL: @test_vdotq_s32(
int32x4_t test_vdotq_s32(int32x4_t acc, int8x16_t a, int8x16_t b) {
// LLVM: call <4 x i32> @llvm.aarch64.neon.sdot.v4i32.v16i8(<4 x i32> {{.*}}, <16 x i8> {{.*}}, <16 x i8> {{.*}})
  return vdotq_s32(acc, a, b);
}

// LLVM-LABEL: @test_vdot_u32(
uint32x2_t test_vdot_u32(uint32x2_t acc, uint8x8_t a, uint8x8_t b) {
// LLVM: call <2 x i32> @llvm.aarch64.neon.udot.v2i32.v8i8(<2 x i32> {{.*}}, <8 x i8> {{.*}}, <8 x i8> {{.*}})
  return vdot_u32(acc, a, b);
}
