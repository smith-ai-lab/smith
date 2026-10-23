__extension__({ extern typeof(* (* (shape_Params * *) 0)) shape_gradient__loss_shapexxParams_P_shapexxBatch_P(shape_Params * x, [[maybe_unused]] shape_Batch * rest_0) __asm__("_shape::gradient(main_loss, shape_Params *, shape_Batch *)@src/main.smith"); shape_gradient__loss_shapexxParams_P_shapexxBatch_P(& p, & batch); })
__extension__({ extern typeof(* (* (shape_Params * *) 0)) shape_gradient__plain_shapexxParams_P(shape_Params * x) __asm__("_shape::gradient(main_plain, shape_Params *)@src/main.smith"); shape_gradient__plain_shapexxParams_P(& p); })
extern typeof(* (* (shape_Params * *) 0)) shape_gradient__loss_shapexxParams_P_shapexxBatch_P(shape_Params * x, [[maybe_unused]] shape_Batch * rest_0) __asm__("_shape::gradient(main_loss, shape_Params *, shape_Batch *)@src/main.smith");
[[gnu::weak]][[gnu::visibility("hidden")]] typeof(* (* (shape_Params * *) 0)) shape_gradient__loss_shapexxParams_P_shapexxBatch_P(shape_Params * x, [[maybe_unused]] shape_Batch * rest_0) {
    (void) sizeof(main_loss(x, rest_0));
    typeof(* x) dx = {
    }
    ;
    dx.a = main_loss(x, rest_0);
    printf("%d %zu\n", (void *) main_loss != nullptr, 1);
    return dx;
}
extern typeof(* (* (shape_Params * *) 0)) shape_gradient__plain_shapexxParams_P(shape_Params * x) __asm__("_shape::gradient(main_plain, shape_Params *)@src/main.smith");
[[gnu::weak]][[gnu::visibility("hidden")]] typeof(* (* (shape_Params * *) 0)) shape_gradient__plain_shapexxParams_P(shape_Params * x) {
    (void) sizeof(main_plain(x));
    typeof(* x) dx = {
    }
    ;
    dx.a = main_plain(x);
    printf("%d %zu\n", (void *) main_plain != nullptr, 0);
    return dx;
}
