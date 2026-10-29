// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s
// RUN: cir-opt %t.cir --verify-roundtrip -o /dev/null

// Mutually recursive records: whichever alias is printed first has to spell
// the other inline. Its body is printed once; later occurrences use its name,
// so the output stays linear in the number of members.

struct Env;
struct Iface {
  int (*a)(Env *);
  int (*b)(Env *);
  int (*c)(Env *);
};
struct Env {
  const Iface *fns;
};
int call(Env *e) { return e->fns->a(e) + e->fns->b(e) + e->fns->c(e); }

// CHECK: !rec_Iface = !cir.struct<"Iface" {data !cir.ptr<!cir.func<(!cir.ptr<!cir.struct<"Env" {data !cir.ptr<!cir.struct<"Iface">>}>>) -> !s32i>>, data !cir.ptr<!cir.func<(!cir.ptr<!cir.struct<"Env">>) -> !s32i>>, data !cir.ptr<!cir.func<(!cir.ptr<!cir.struct<"Env">>) -> !s32i>>}>
// CHECK: !rec_Env = !cir.struct<"Env" {data !cir.ptr<!rec_Iface>}>
