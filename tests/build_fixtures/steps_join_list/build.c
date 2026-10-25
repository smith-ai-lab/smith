#include <smith/build.h>

static const char *const *make_list(SmithBuild *b) {
    {
        return smith_list(b, "one", "two words", "$three", nullptr);
    }
}

static const char *const *kept(SmithBuild *b) {
    const char *const *list;
    {
        list = smith_list(b, "kept", "alive", nullptr);
        const char *scratch[] = {"gone", nullptr};
        (void)scratch;
    }
    return list;
}

void build(SmithBuild *b) {
    const char *const *empty = smith_list(b, nullptr);
    const char *const *one = smith_list(b, "solo", nullptr);
    const char *const *many = make_list(b);
    smith_write(b, "join.txt",
                smith_fmt(b, "[%s][%s][%s][%s][%s]\n[%s]\n", smith_join(b, empty, ","), smith_join(b, one, ", "),
                          smith_join(b, many, ", "), smith_join(b, many, ""), smith_join(b, kept(b), "\n"),
                          smith_join(b, smith_map(b, many, "#include \"%s\"\n"), "")));
    smith_write(b, "list.txt", smith_fmt(b, "%d %d %d %s\n", (int)(empty[0] == nullptr), (int)(one[1] == nullptr),
                                         (int)(many[3] == nullptr), many[2]));
    smith_default(b, smith_list(b, nullptr));
}
