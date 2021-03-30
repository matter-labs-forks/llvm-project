; RUN: llc < %s | FileCheck %s

target datalayout = "e-p:256:256-i256:256:256"
target triple = "syncvm"

; CHECK-LABEL: shl_smallconst
define i256 @shl_smallconst(i256 %par) nounwind {
; CHECK: movl	1024, r2
; CHECK: movh	0, r2
; CHECK: mul	r1, r2, r1, r0
  %1 = shl i256 %par, 10
  ret i256 %1
}

; CHECK-LABEL: lshr_smallconst
define i256 @lshr_smallconst(i256 %par) nounwind {
; CHECK: movl	1024, r2
; CHECK: movh	0, r2
; CHECK: div	r1, r2, r1, r0
  %1 = lshr i256 %par, 10
  ret i256 %1
}
