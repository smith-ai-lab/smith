// Plain C with a tag and a union and no `smith::enum` annotation: the plugin leaves it alone.
typedef struct {
    enum { circle_tag, rect_tag } tag;
    union {
        float circle;
        float rect;
    };
} Shape;
float read(Shape s) {
    return s.circle;
}
float read_checked(Shape *p) {
    if (p->tag == rect_tag) return p->circle;
    return p->rect;
}
