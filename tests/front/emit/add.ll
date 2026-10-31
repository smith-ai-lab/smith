
define i32 @"front::test::add"(i32 %a, i32 %b) {
entry:
  %a1 = alloca i32, align 4
  %b2 = alloca i32, align 4
  br label %body

body:                                             ; preds = %entry
  store i32 %a, ptr %a1, align 4
  store i32 %b, ptr %b2, align 4
  %0 = load i32, ptr %a1, align 4
  %1 = load i32, ptr %b2, align 4
  %2 = call { i32, i1 } @llvm.sadd.with.overflow.i32(i32 %0, i32 %1)
  %overflow = extractvalue { i32, i1 } %2, 1
  br i1 %overflow, label %trap, label %ok

trap:                                             ; preds = %body
  call void @llvm.trap()
  unreachable

ok:                                               ; preds = %body
  %3 = extractvalue { i32, i1 } %2, 0
  ret i32 %3
}

; Function Attrs: nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none)
declare { i32, i1 } @llvm.sadd.with.overflow.i32(i32, i32) #0

; Function Attrs: cold noreturn nounwind memory(inaccessiblemem: write)
declare void @llvm.trap() #1

attributes #0 = { nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none) }
attributes #1 = { cold noreturn nounwind memory(inaccessiblemem: write) }
