; ============================================================
; WORKED EXAMPLE — SIMPL → LLVM IR
; ============================================================
;
; SIMPL source (test.simpl):
;
;   let x be 10
;   while x > 0 do
;       if x > 5 then
;           set x to x - 2
;       else
;           set x to x - 1
;       end
;   end
;   print x
;
; Expected LLVM IR output (output.ll):
; ============================================================

target triple = "x86_64-pc-linux-gnu"

@fmt = private unnamed_addr constant [4 x i8] c"%d\0A\00"

declare i32 @printf(i8*, ...)

define i32 @main() {
entry:
  %x_slot = alloca i32, align 4        ; alloca for user var 'x'

  store i32 10, i32* %x_slot, align 4  ; let x be 10

  br label %L0                          ; fallthrough into while loop

L0:                                     ; while loop header
  %r0 = load i32, i32* %x_slot, align 4
  %cmp0 = icmp sgt i32 %r0, 0
  %t0 = zext i1 %cmp0 to i32           ; t0 = x > 0
  %cond1 = icmp ne i32 %t0, 0
  br i1 %cond1, label %L2, label %L1   ; if_false t0 goto L1

L2:                                     ; while body
  %r1 = load i32, i32* %x_slot, align 4
  %cmp2 = icmp sgt i32 %r1, 5
  %t1 = zext i1 %cmp2 to i32           ; t1 = x > 5
  %cond3 = icmp ne i32 %t1, 0
  br i1 %cond3, label %L4, label %L3   ; if_false t1 goto L3

L4:                                     ; then branch: x = x - 2
  %r2 = load i32, i32* %x_slot, align 4
  %t2 = sub i32 %r2, 2
  store i32 %t2, i32* %x_slot, align 4
  br label %L5

L3:                                     ; else branch: x = x - 1
  %r3 = load i32, i32* %x_slot, align 4
  %t3 = sub i32 %r3, 1
  store i32 %t3, i32* %x_slot, align 4

L5:                                     ; end if
  br label %L0                          ; goto L0 (back-edge)

L1:                                     ; while exit
  %r4 = load i32, i32* %x_slot, align 4
  %gep0 = getelementptr [4 x i8], [4 x i8]* @fmt, i32 0, i32 0
  call i32 (i8*, ...) @printf(i8* %gep0, i32 %r4)

  ret i32 0
}

; ============================================================
; To compile and run:
;
;   ./simpl test.simpl output.ll
;   clang output.ll -o program
;   ./program
;   → prints: 0
;
; To see LLVM's own optimizations on top of SIMPL's:
;
;   opt -mem2reg -O2 output.ll -o optimized.bc
;   llc optimized.bc -o program.s
;   clang program.s -o program
; ============================================================
