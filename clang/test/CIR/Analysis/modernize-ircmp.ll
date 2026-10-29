; cir-modernize-ircmp undoes only what a spelling change does to the IR. Each
; fail case differs from the pass case's original in exactly one other way.

; RUN: rm -rf %t && split-file %s %t && cd %t
; RUN: cir-modernize-ircmp orig.ll ref.ll
; RUN: not cir-modernize-ircmp orig.ll inst.ll | FileCheck --check-prefix=INST %s
; RUN: not cir-modernize-ircmp orig.ll loop.ll | FileCheck --check-prefix=LOOP %s
; RUN: not cir-modernize-ircmp orig.ll name.ll | FileCheck --check-prefix=NAME %s
; RUN: not cir-modernize-ircmp orig.ll noalias.ll | FileCheck --check-prefix=NOALIAS %s
; RUN: not cir-modernize-ircmp orig.ll range.ll | FileCheck --check-prefix=RANGE %s
; RUN: not cir-modernize-ircmp orig.ll fnalign.ll | FileCheck --check-prefix=FNALIGN %s
; RUN: not cir-modernize-ircmp orig.ll extra.ll | FileCheck --check-prefix=EXTRA %s
; RUN: cir-modernize-ircmp method.ll method-const.ll
; RUN: cir-modernize-ircmp typeinfo.ll typeinfo-ref.ll
; RUN: not cir-modernize-ircmp typeinfo.ll typeinfo-other.ll | FileCheck --check-prefix=TYPEINFO %s
; RUN: not cir-modernize-ircmp method.ll method-other.ll | FileCheck --check-prefix=METHOD %s

; INST:      in define internal noundef i32 @_ZL5firstP4Node
; INST-NEXT: - %2 = add i32 %1, 1
; INST-NEXT: + %2 = sub i32 %1, 1
; LOOP:      + !#{{[0-9]+}} = !{!"llvm.loop.unroll.disable"}
; NAME:      symbol {{[0-9]+}}: first(Node*) vs second(Node&)
; NOALIAS:   + define internal noundef i32 @_ZL5firstP4Node(ptr noalias noundef %head)
; RANGE:     + %1 = load i32, ptr %v, align 8, !range
; FNALIGN:   + define {{.*}} align 16 {
; EXTRA:     global count
; METHOD:    symbol {{[0-9]+}}: S::get() vs S::got() const
; TYPEINFO:  + @"_ZTSZ5firstP4NodeE3$_0" = {{.*}}c"Z5firstR4NodeE3$_1\00"

;--- orig.ll
%struct.Node = type { i32, ptr }

$_Z4peekP4Node = comdat any

@__func__._ZL5firstP4Node = private constant [6 x i8] c"first\00"

define internal noundef i32 @_ZL5firstP4Node(ptr noundef %head) {
entry:
  %head.addr = alloca ptr, align 8
  store ptr %head, ptr %head.addr, align 8
  %0 = load ptr, ptr %head.addr, align 8
  %v = getelementptr inbounds %struct.Node, ptr %0, i32 0, i32 0
  %1 = load i32, ptr %v, align 8
  %2 = add i32 %1, 1
  br label %exit, !llvm.loop !0

exit:
  ret i32 %2
}

define linkonce_odr i32 @_Z4peekP4Node(ptr %p) comdat {
  ret i32 0
}

define internal i32 @_ZL4bothP4Node(ptr %p) {
  ret i32 1
}

define i32 @use(ptr %n) {
  %r = call noundef i32 @_ZL5firstP4Node(ptr noundef %n)
  %s = call i32 @_Z4peekP4Node(ptr %n)
  ret i32 %r
}

!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.mustprogress"}

;--- ref.ll
%struct.Node.5 = type { i32, ptr }

$_Z4peekR4Node = comdat any

@__func__._ZL5firstR4Node = private constant [6 x i8] c"first\00"

define internal noundef i32 @_ZL5firstR4Node(ptr noundef nonnull align 8 dereferenceable(16) %head) {
entry:
  %head.addr = alloca ptr, align 8
  store ptr %head, ptr %head.addr, align 8
  %0 = load ptr, ptr %head.addr, align 8, !nonnull !0, !align !1, !noundef !0
  %v = getelementptr inbounds %struct.Node.5, ptr %0, i32 0, i32 0
  %1 = load i32, ptr %v, align 8
  %2 = add i32 %1, 1
  br label %exit, !llvm.loop !2

exit:
  ret i32 %2
}

define linkonce_odr i32 @_Z4peekR4Node(ptr nonnull %p) comdat {
  ret i32 0
}

; pointer to reference and to const at once: Node const&
define internal i32 @_ZL4bothRK4Node(ptr nonnull %p) {
  ret i32 1
}

define i32 @use(ptr %n) {
  %r = call noundef i32 @_ZL5firstR4Node(ptr noundef nonnull align 8 dereferenceable(16) %n)
  %s = call i32 @_Z4peekR4Node(ptr nonnull %n)
  ret i32 %r
}

!0 = !{}
!1 = !{i64 8}
!2 = distinct !{!2, !3}
!3 = !{!"llvm.loop.mustprogress"}
;--- inst.ll
%struct.Node = type { i32, ptr }

$_Z4peekP4Node = comdat any

@__func__._ZL5firstP4Node = private constant [6 x i8] c"first\00"

define internal noundef i32 @_ZL5firstP4Node(ptr noundef %head) {
entry:
  %head.addr = alloca ptr, align 8
  store ptr %head, ptr %head.addr, align 8
  %0 = load ptr, ptr %head.addr, align 8
  %v = getelementptr inbounds %struct.Node, ptr %0, i32 0, i32 0
  %1 = load i32, ptr %v, align 8
  %2 = sub i32 %1, 1
  br label %exit, !llvm.loop !0

exit:
  ret i32 %2
}

define linkonce_odr i32 @_Z4peekP4Node(ptr %p) comdat {
  ret i32 0
}

define internal i32 @_ZL4bothP4Node(ptr %p) {
  ret i32 1
}

define i32 @use(ptr %n) {
  %r = call noundef i32 @_ZL5firstP4Node(ptr noundef %n)
  %s = call i32 @_Z4peekP4Node(ptr %n)
  ret i32 %r
}

!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.mustprogress"}

;--- loop.ll
%struct.Node = type { i32, ptr }

$_Z4peekP4Node = comdat any

@__func__._ZL5firstP4Node = private constant [6 x i8] c"first\00"

define internal noundef i32 @_ZL5firstP4Node(ptr noundef %head) {
entry:
  %head.addr = alloca ptr, align 8
  store ptr %head, ptr %head.addr, align 8
  %0 = load ptr, ptr %head.addr, align 8
  %v = getelementptr inbounds %struct.Node, ptr %0, i32 0, i32 0
  %1 = load i32, ptr %v, align 8
  %2 = add i32 %1, 1
  br label %exit, !llvm.loop !0

exit:
  ret i32 %2
}

define linkonce_odr i32 @_Z4peekP4Node(ptr %p) comdat {
  ret i32 0
}

define internal i32 @_ZL4bothP4Node(ptr %p) {
  ret i32 1
}

define i32 @use(ptr %n) {
  %r = call noundef i32 @_ZL5firstP4Node(ptr noundef %n)
  %s = call i32 @_Z4peekP4Node(ptr %n)
  ret i32 %r
}

!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.unroll.disable"}

;--- name.ll
%struct.Node = type { i32, ptr }

$_Z4peekP4Node = comdat any

@__func__._ZL5firstP4Node = private constant [6 x i8] c"first\00"

define internal noundef i32 @_ZL6secondR4Node(ptr noundef %head) {
entry:
  %head.addr = alloca ptr, align 8
  store ptr %head, ptr %head.addr, align 8
  %0 = load ptr, ptr %head.addr, align 8
  %v = getelementptr inbounds %struct.Node, ptr %0, i32 0, i32 0
  %1 = load i32, ptr %v, align 8
  %2 = add i32 %1, 1
  br label %exit, !llvm.loop !0

exit:
  ret i32 %2
}

define linkonce_odr i32 @_Z4peekP4Node(ptr %p) comdat {
  ret i32 0
}

define internal i32 @_ZL4bothP4Node(ptr %p) {
  ret i32 1
}

define i32 @use(ptr %n) {
  %r = call noundef i32 @_ZL6secondR4Node(ptr noundef %n)
  %s = call i32 @_Z4peekP4Node(ptr %n)
  ret i32 %r
}

!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.mustprogress"}

;--- noalias.ll
%struct.Node = type { i32, ptr }

$_Z4peekP4Node = comdat any

@__func__._ZL5firstP4Node = private constant [6 x i8] c"first\00"

define internal noundef i32 @_ZL5firstP4Node(ptr noalias noundef %head) {
entry:
  %head.addr = alloca ptr, align 8
  store ptr %head, ptr %head.addr, align 8
  %0 = load ptr, ptr %head.addr, align 8
  %v = getelementptr inbounds %struct.Node, ptr %0, i32 0, i32 0
  %1 = load i32, ptr %v, align 8
  %2 = add i32 %1, 1
  br label %exit, !llvm.loop !0

exit:
  ret i32 %2
}

define linkonce_odr i32 @_Z4peekP4Node(ptr %p) comdat {
  ret i32 0
}

define internal i32 @_ZL4bothP4Node(ptr %p) {
  ret i32 1
}

define i32 @use(ptr %n) {
  %r = call noundef i32 @_ZL5firstP4Node(ptr noundef %n)
  %s = call i32 @_Z4peekP4Node(ptr %n)
  ret i32 %r
}

!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.mustprogress"}

;--- range.ll
%struct.Node = type { i32, ptr }

$_Z4peekP4Node = comdat any

@__func__._ZL5firstP4Node = private constant [6 x i8] c"first\00"

define internal noundef i32 @_ZL5firstP4Node(ptr noundef %head) {
entry:
  %head.addr = alloca ptr, align 8
  store ptr %head, ptr %head.addr, align 8
  %0 = load ptr, ptr %head.addr, align 8
  %v = getelementptr inbounds %struct.Node, ptr %0, i32 0, i32 0
  %1 = load i32, ptr %v, align 8, !range !2
  %2 = add i32 %1, 1
  br label %exit, !llvm.loop !0

exit:
  ret i32 %2
}

define linkonce_odr i32 @_Z4peekP4Node(ptr %p) comdat {
  ret i32 0
}

define internal i32 @_ZL4bothP4Node(ptr %p) {
  ret i32 1
}

define i32 @use(ptr %n) {
  %r = call noundef i32 @_ZL5firstP4Node(ptr noundef %n)
  %s = call i32 @_Z4peekP4Node(ptr %n)
  ret i32 %r
}

!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.mustprogress"}
!2 = !{i32 0, i32 10}

;--- fnalign.ll
%struct.Node = type { i32, ptr }

$_Z4peekP4Node = comdat any

@__func__._ZL5firstP4Node = private constant [6 x i8] c"first\00"

define internal noundef i32 @_ZL5firstP4Node(ptr noundef %head) {
entry:
  %head.addr = alloca ptr, align 8
  store ptr %head, ptr %head.addr, align 8
  %0 = load ptr, ptr %head.addr, align 8
  %v = getelementptr inbounds %struct.Node, ptr %0, i32 0, i32 0
  %1 = load i32, ptr %v, align 8
  %2 = add i32 %1, 1
  br label %exit, !llvm.loop !0

exit:
  ret i32 %2
}

define linkonce_odr i32 @_Z4peekP4Node(ptr %p) comdat {
  ret i32 0
}

define internal i32 @_ZL4bothP4Node(ptr %p) {
  ret i32 1
}

define i32 @use(ptr %n) align 16 {
  %r = call noundef i32 @_ZL5firstP4Node(ptr noundef %n)
  %s = call i32 @_Z4peekP4Node(ptr %n)
  ret i32 %r
}

!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.mustprogress"}

;--- extra.ll
%struct.Node = type { i32, ptr }

$_Z4peekP4Node = comdat any

@__func__._ZL5firstP4Node = private constant [6 x i8] c"first\00"
@extra = global i32 0

define internal noundef i32 @_ZL5firstP4Node(ptr noundef %head) {
entry:
  %head.addr = alloca ptr, align 8
  store ptr %head, ptr %head.addr, align 8
  %0 = load ptr, ptr %head.addr, align 8
  %v = getelementptr inbounds %struct.Node, ptr %0, i32 0, i32 0
  %1 = load i32, ptr %v, align 8
  %2 = add i32 %1, 1
  br label %exit, !llvm.loop !0

exit:
  ret i32 %2
}

define linkonce_odr i32 @_Z4peekP4Node(ptr %p) comdat {
  ret i32 0
}

define internal i32 @_ZL4bothP4Node(ptr %p) {
  ret i32 1
}

define i32 @use(ptr %n) {
  %r = call noundef i32 @_ZL5firstP4Node(ptr noundef %n)
  %s = call i32 @_Z4peekP4Node(ptr %n)
  ret i32 %r
}

!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.mustprogress"}

;--- method.ll
%struct.S = type { i32 }

define i32 @_ZN1S3getEv(ptr noundef %this) {
  %v = load i32, ptr %this, align 4
  ret i32 %v
}

;--- method-const.ll
%struct.S = type { i32 }

define i32 @_ZNK1S3getEv(ptr noundef %this) {
  %v = load i32, ptr %this, align 4
  ret i32 %v
}

;--- method-other.ll
%struct.S = type { i32 }

define i32 @_ZNK1S3gotEv(ptr noundef %this) {
  %v = load i32, ptr %this, align 4
  ret i32 %v
}
;--- typeinfo.ll
@_ZTSZ5firstP4NodeE3$_0 = internal constant [19 x i8] c"Z5firstP4NodeE3$_0\00"

define ptr @_Z5firstP4Node(ptr %n) {
  ret ptr @_ZTSZ5firstP4NodeE3$_0
}

;--- typeinfo-ref.ll
@_ZTSZ5firstR4NodeE3$_0 = internal constant [19 x i8] c"Z5firstR4NodeE3$_0\00"

define ptr @_Z5firstR4Node(ptr %n) {
  ret ptr @_ZTSZ5firstR4NodeE3$_0
}

;--- typeinfo-other.ll
@_ZTSZ5firstR4NodeE3$_0 = internal constant [19 x i8] c"Z5firstR4NodeE3$_1\00"

define ptr @_Z5firstR4Node(ptr %n) {
  ret ptr @_ZTSZ5firstR4NodeE3$_0
}
