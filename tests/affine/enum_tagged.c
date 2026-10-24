// What the lowering writes, in C: the annotation is what makes the struct a payload enum (the rule is the same).
typedef struct Shape {
    enum Shape_tag { Shape_tag_circle, Shape_tag_rect } tag;
    union {
        float circle;
        float rect;
    };
} __attribute__((annotate("smith::enum"))) Shape;
float unchecked(Shape s) {
    return s.circle;
}
float checked(Shape *p) {
    if (p->tag == Shape_tag_circle) return p->circle;
    switch (p->tag) {
    case Shape_tag_rect:
        return p->rect;
    case Shape_tag_circle:
        break;
    }
    return 0;
}
