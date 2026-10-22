__INT32_TYPE__ main(void) {
shape_box_Box b = {.width = 3, .height = 4};
printf("%d %d %d %d %d %d\n", shape_box_area(&b, 5), shape_box_scaled(2, 3, 4), shape_box_apply(shape_box_width, &b),
shape_box_combined(10), shape_box_height(&b), shape_box_larger(__INT32_TYPE__, 3, 9));
return 0;
}
