; ModuleID = 't.smith'
source_filename = "t.smith"

@text = private unnamed_addr constant [2 x i8] c"-\00", align 1
@text.1 = private unnamed_addr constant [4 x i8] c"i32\00", align 1
@at = private unnamed_addr constant [12 x i8] c" at t.smith\00", align 1

define internal ptr @countdown(i32 %0) {
entry:
  %args = alloca { i32 }, align 8
  br label %body

body:                                             ; preds = %entry
  %field = getelementptr inbounds nuw { i32 }, ptr %args, i32 0, i32 0
  store i32 %0, ptr %field, align 4
  %1 = call ptr @smith_stream_start(ptr @"countdown::body", ptr %args, i64 ptrtoint (ptr getelementptr ({ i32 }, ptr null, i32 1) to i64))
  ret ptr %1
}

define internal void @"countdown::body"(ptr %0, ptr %1) {
entry:
  %"$stream" = alloca ptr, align 8
  %from = alloca i32, align 4
  %n = alloca i32, align 4
  br label %body

body:                                             ; preds = %entry
  store ptr %0, ptr %"$stream", align 8
  %field = getelementptr inbounds nuw { i32 }, ptr %1, i32 0, i32 0
  %arg = load i32, ptr %field, align 4
  store i32 %arg, ptr %from, align 4
  %2 = load i32, ptr %from, align 4
  store i32 %2, ptr %n, align 4
  br label %whilecond

whilecond:                                        ; preds = %ok, %body
  %3 = load i32, ptr %n, align 4
  %4 = icmp sgt i32 %3, 0
  br i1 %4, label %whilebody, label %endwhile

whilebody:                                        ; preds = %whilecond
  %5 = load i32, ptr %n, align 4
  %stream = load ptr, ptr %"$stream", align 8
  %6 = call ptr @smith_stream_slot(ptr %stream)
  store i32 %5, ptr %6, align 4
  %7 = call zeroext i1 @smith_stream_yield(ptr %stream)
  br i1 %7, label %yield.go, label %yield.closed

endwhile:                                         ; preds = %whilecond
  %stream2 = load ptr, ptr %"$stream", align 8
  call void @smith_stream_finish(ptr %stream2)
  unreachable

yield.go:                                         ; preds = %whilebody
  %8 = load i32, ptr %n, align 4
  %9 = call { i32, i1 } @llvm.ssub.with.overflow.i32(i32 %8, i32 1)
  %overflow = extractvalue { i32, i1 } %9, 1
  %10 = extractvalue { i32, i1 } %9, 0
  br i1 %overflow, label %fail, label %ok, !prof !0

yield.closed:                                     ; preds = %whilebody
  %stream1 = load ptr, ptr %"$stream", align 8
  call void @smith_stream_finish(ptr %stream1)
  unreachable

fail:                                             ; preds = %yield.go
  %11 = sext i32 %8 to i64
  call void @smith_contract_fail_overflow(ptr @text, i64 %11, i64 1, ptr @text.1, ptr @at)
  br label %trap

ok:                                               ; preds = %yield.go
  store i32 %10, ptr %n, align 4
  br label %whilecond

trap:                                             ; preds = %fail
  call void @llvm.trap()
  unreachable
}

declare ptr @smith_stream_start(ptr, ptr, i64)

declare ptr @smith_stream_slot(ptr)

declare zeroext i1 @smith_stream_yield(ptr)

declare void @smith_stream_finish(ptr)

; Function Attrs: nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none)
declare { i32, i1 } @llvm.ssub.with.overflow.i32(i32, i32) #0

declare void @smith_contract_fail_overflow(ptr, i64, i64, ptr, ptr)

; Function Attrs: cold noreturn nounwind memory(inaccessiblemem: write)
declare void @llvm.trap() #1

attributes #0 = { nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none) }
attributes #1 = { cold noreturn nounwind memory(inaccessiblemem: write) }

!0 = !{!"branch_weights", i32 1, i32 2000}
