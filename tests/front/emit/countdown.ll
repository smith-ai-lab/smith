; ModuleID = 't.smith'
source_filename = "t.smith"

@text = private unnamed_addr constant [2 x i8] c"-\00", align 1
@text.1 = private unnamed_addr constant [4 x i8] c"i32\00", align 1
@at = private unnamed_addr constant [12 x i8] c" at t.smith\00", align 1

define ptr @countdown(i32 %0) {
entry:
  %args = alloca { i32 }, align 8
  br label %body

body:                                             ; preds = %entry
  %arg = getelementptr inbounds nuw { i32 }, ptr %args, i32 0, i32 0
  store i32 %0, ptr %arg, align 4
  %start = call ptr @smith_stream_start(ptr @"countdown::body", ptr %args, i64 ptrtoint (ptr getelementptr ({ i32 }, ptr null, i32 1) to i64))
  ret ptr %start
}

define internal void @"countdown::body"(ptr %0, ptr %1) {
entry:
  %from = alloca i32, align 4
  %n = alloca i32, align 4
  br label %body

body:                                             ; preds = %entry
  %arg = getelementptr inbounds nuw { i32 }, ptr %1, i32 0, i32 0
  %arg_value = load i32, ptr %arg, align 4
  store i32 %arg_value, ptr %from, align 4
  %2 = load i32, ptr %from, align 4
  store i32 %2, ptr %n, align 4
  br label %whilecond

stream_end:                                       ; preds = %endwhile, %yield_closed
  call void @smith_stream_finish(ptr %0)
  unreachable

whilecond:                                        ; preds = %ok, %body
  %3 = load i32, ptr %n, align 4
  %4 = icmp sgt i32 %3, 0
  br i1 %4, label %whilebody, label %endwhile

whilebody:                                        ; preds = %whilecond
  %5 = load i32, ptr %n, align 4
  %slot = call ptr @smith_stream_slot(ptr %0)
  store i32 %5, ptr %slot, align 4
  %yield = call i1 @smith_stream_yield(ptr %0)
  br i1 %yield, label %yield_on, label %yield_closed

endwhile:                                         ; preds = %whilecond
  br label %stream_end

yield_on:                                         ; preds = %whilebody
  %6 = load i32, ptr %n, align 4
  %7 = call { i32, i1 } @llvm.ssub.with.overflow.i32(i32 %6, i32 1)
  %overflow = extractvalue { i32, i1 } %7, 1
  %8 = extractvalue { i32, i1 } %7, 0
  br i1 %overflow, label %fail, label %ok, !prof !0

yield_closed:                                     ; preds = %whilebody
  br label %stream_end

fail:                                             ; preds = %yield_on
  %9 = sext i32 %6 to i64
  call void @smith_contract_fail_overflow(ptr @text, i64 %9, i64 1, ptr @text.1, ptr @at)
  br label %trap

ok:                                               ; preds = %yield_on
  store i32 %8, ptr %n, align 4
  br label %whilecond

trap:                                             ; preds = %fail
  call void @llvm.trap()
  unreachable
}

declare ptr @smith_stream_start(ptr, ptr, i64)

declare ptr @smith_stream_slot(ptr)

declare i1 @smith_stream_yield(ptr)

; Function Attrs: nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none)
declare { i32, i1 } @llvm.ssub.with.overflow.i32(i32, i32) #0

declare void @smith_contract_fail_overflow(ptr, i64, i64, ptr, ptr)

; Function Attrs: cold noreturn nounwind memory(inaccessiblemem: write)
declare void @llvm.trap() #1

declare void @smith_stream_finish(ptr)

attributes #0 = { nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none) }
attributes #1 = { cold noreturn nounwind memory(inaccessiblemem: write) }

!0 = !{!"branch_weights", i32 1, i32 2000}
