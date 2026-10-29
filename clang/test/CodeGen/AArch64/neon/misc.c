// REQUIRES: aarch64-registered-target || arm-registered-target

// RUN:                   %clang_cc1_cg_arm64_neon           -target-feature +aes -target-feature +crc -emit-llvm %s -disable-O0-optnone | opt -S -passes=mem2reg,sroa,instcombine | FileCheck %s --check-prefix=LLVM
// RUN: %if cir-enabled %{%clang_cc1_cg_arm64_neon -target-feature +aes -target-feature +crc -fclangir -emit-llvm %s -disable-O0-optnone | opt -S -passes=mem2reg,sroa,instcombine | FileCheck %s --check-prefix=LLVM %}
// RUN: %if cir-enabled %{%clang_cc1_cg_arm64_neon -target-feature +aes -target-feature +crc -fclangir -emit-cir  %s -disable-O0-optnone |                               FileCheck %s --check-prefix=CIR %}

// Extract, pairwise add, compare against zero, popcount and the AES rounds:
// each the same LLVM IR through ClangIR as through classic codegen.

#include <arm_neon.h>

// LLVM-LABEL: @test_vextq_u8(
// CIR-LABEL: @test_vextq_u8(
uint8x16_t test_vextq_u8(uint8x16_t a, uint8x16_t b) {
// CIR: cir.vec.shuffle
// LLVM: shufflevector <16 x i8> {{.*}}, <16 x i8> {{.*}}, <16 x i32> <i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15, i32 16, i32 17, i32 18>
  return vextq_u8(a, b, 3);
}

// LLVM-LABEL: @test_vext_s16(
// CIR-LABEL: @test_vext_s16(
int16x4_t test_vext_s16(int16x4_t a, int16x4_t b) {
// LLVM: shufflevector <4 x i16> {{.*}}, <4 x i16> {{.*}}, <4 x i32> <i32 1, i32 2, i32 3, i32 4>
  return vext_s16(a, b, 1);
}

// LLVM-LABEL: @test_vpaddq_u32(
uint32x4_t test_vpaddq_u32(uint32x4_t a, uint32x4_t b) {
// LLVM: call <4 x i32> @llvm.aarch64.neon.addp.v4i32(<4 x i32> {{.*}}, <4 x i32> {{.*}})
  return vpaddq_u32(a, b);
}

// LLVM-LABEL: @test_vpaddq_f32(
float32x4_t test_vpaddq_f32(float32x4_t a, float32x4_t b) {
// LLVM: call <4 x float> @llvm.aarch64.neon.faddp.v4f32(<4 x float> {{.*}}, <4 x float> {{.*}})
  return vpaddq_f32(a, b);
}

// LLVM-LABEL: @test_vcltzq_s32(
// CIR-LABEL: @vcltzq_s32(
uint32x4_t test_vcltzq_s32(int32x4_t a) {
// CIR: cir.vec.cmp(lt,
// LLVM: ashr <4 x i32> {{.*}}, splat (i32 31)
  return vcltzq_s32(a);
}

// LLVM-LABEL: @test_vcgezq_f32(
uint32x4_t test_vcgezq_f32(float32x4_t a) {
// LLVM: fcmp oge <4 x float> {{.*}}, zeroinitializer
  return vcgezq_f32(a);
}

// LLVM-LABEL: @test_vclez_s8(
uint8x8_t test_vclez_s8(int8x8_t a) {
// LLVM: icmp slt <8 x i8> {{.*}}, splat (i8 1)
  return vclez_s8(a);
}

// LLVM-LABEL: @test_vcgtz_s16(
uint16x4_t test_vcgtz_s16(int16x4_t a) {
// LLVM: icmp sgt <4 x i16> {{.*}}, zeroinitializer
  return vcgtz_s16(a);
}

// LLVM-LABEL: @test_vcnt_u8(
uint8x8_t test_vcnt_u8(uint8x8_t a) {
// LLVM: call {{.*}}<8 x i8> @llvm.ctpop.v8i8(<8 x i8> {{.*}})
  return vcnt_u8(a);
}

// LLVM-LABEL: @test_vcntq_s8(
int8x16_t test_vcntq_s8(int8x16_t a) {
// LLVM: call {{.*}}<16 x i8> @llvm.ctpop.v16i8(<16 x i8> {{.*}})
  return vcntq_s8(a);
}

// LLVM-LABEL: @test_vaesmcq_u8(
uint8x16_t test_vaesmcq_u8(uint8x16_t a) {
// LLVM: call <16 x i8> @llvm.aarch64.crypto.aesmc(<16 x i8> {{.*}})
  return vaesmcq_u8(a);
}

// LLVM-LABEL: @test_vaesimcq_u8(
uint8x16_t test_vaesimcq_u8(uint8x16_t a) {
// LLVM: call <16 x i8> @llvm.aarch64.crypto.aesimc(<16 x i8> {{.*}})
  return vaesimcq_u8(a);
}

// LLVM-LABEL: @test_vaeseq_u8(
uint8x16_t test_vaeseq_u8(uint8x16_t data, uint8x16_t key) {
// LLVM: call <16 x i8> @llvm.aarch64.crypto.aese(<16 x i8> {{.*}}, <16 x i8> {{.*}})
  return vaeseq_u8(data, key);
}

// LLVM-LABEL: @test_vqtbl2q_u8(
uint8x16_t test_vqtbl2q_u8(uint8x16x2_t t, uint8x16_t idx) {
// LLVM: call <16 x i8> @llvm.aarch64.neon.tbl2.v16i8(<16 x i8> {{.*}}, <16 x i8> {{.*}}, <16 x i8> {{.*}})
  return vqtbl2q_u8(t, idx);
}

// LLVM-LABEL: @test_vqtbl1_u8(
uint8x8_t test_vqtbl1_u8(uint8x16_t t, uint8x8_t idx) {
// LLVM: call <8 x i8> @llvm.aarch64.neon.tbl1.v8i8(<16 x i8> {{.*}}, <8 x i8> {{.*}})
  return vqtbl1_u8(t, idx);
}

// LLVM-LABEL: @test_vqtbx1q_s8(
int8x16_t test_vqtbx1q_s8(int8x16_t a, int8x16_t t, uint8x16_t idx) {
// LLVM: call <16 x i8> @llvm.aarch64.neon.tbx1.v16i8(<16 x i8> {{.*}}, <16 x i8> {{.*}}, <16 x i8> {{.*}})
  return vqtbx1q_s8(a, t, idx);
}

// LLVM-LABEL: @test_vtstq_u8(
// CIR-LABEL: @vtstq_u8(
uint8x16_t test_vtstq_u8(uint8x16_t a, uint8x16_t b) {
// CIR: cir.and
// CIR: cir.vec.cmp(ne,
// LLVM: [[AND:%.*]] = and <16 x i8> {{.*}}, {{.*}}
// LLVM: [[NE:%.*]] = icmp ne <16 x i8> [[AND]], zeroinitializer
// LLVM: sext <16 x i1> [[NE]] to <16 x i8>
  return vtstq_u8(a, b);
}

// LLVM-LABEL: @test_vtst_s32(
uint32x2_t test_vtst_s32(int32x2_t a, int32x2_t b) {
// LLVM: icmp ne <2 x i32> {{.*}}, zeroinitializer
  return vtst_s32(a, b);
}

// LLVM-LABEL: @test_vmovn_u16(
// CIR-LABEL: @vmovn_u16(
uint8x8_t test_vmovn_u16(uint16x8_t a) {
// CIR: cir.cast integral
// LLVM: trunc <8 x i16> {{.*}} to <8 x i8>
  return vmovn_u16(a);
}

// LLVM-LABEL: @test_vmovn_s64(
int32x2_t test_vmovn_s64(int64x2_t a) {
// LLVM: trunc <2 x i64> {{.*}} to <2 x i32>
  return vmovn_s64(a);
}

// LLVM-LABEL: @test_vmull_p64(
poly128_t test_vmull_p64(poly64_t a, poly64_t b) {
// LLVM: [[P:%.*]] = call <16 x i8> @llvm.aarch64.neon.pmull64(i64 {{.*}}, i64 {{.*}})
// LLVM: bitcast <16 x i8> [[P]] to i128
  return vmull_p64(a, b);
}
