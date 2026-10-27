// Classes and blocks defined from a .smith file (issue #319, skill smith-lang-objc, knowledge/objc.md). Included by
// runtime/lang.c, after runtime/lang_objc.c, whose index readers it uses.
//
// `objc::methods(objc::P, Data)` is a struct with one function pointer for each instance method of the class or protocol
// P (and of its superclasses and protocols), named as #318 names a call and taking `Data *` where Objective-C passes the
// object. The struct, an adapter for each method and a function that adds the methods to a class are C text written in
// front of the first declaration (the index is read there); a use of the type is the name of that struct. The generic
// `objc::class_define` (lib/smith/objc.smith) only loops over its tables and calls the function the table's type picks.
// An object keeps its data in one hidden instance variable, `{self, Data}`: a method receives the `Data *`, and
// `objc::self` takes the object back from it. Each method is an `imp_implementationWithBlock` of a block that holds the
// variable's offset and the user's function, so the offset the runtime chooses at registration costs one load.

#include <stdarg.h>

constexpr size_t smith_lang_objc_fields_max = 2048;
constexpr size_t smith_lang_objc_uses_max = 64;

static SmithLangObjcMethod smith_lang_objc_fields[smith_lang_objc_fields_max];
static uint64_t smith_lang_objc_field_set[smith_lang_objc_set_size];

typedef struct {
    char kind; // 'c' or 'p'
    char owner[96];
    char data[96];
} SmithLangObjcUse;

// ---- The text every file that defines a class or a block gets

constexpr char smith_lang_objc_class_runtime[] =
    "extern void *smith_objc_stack_isa __asm__(\"__NSConcreteStackBlock\");\n"
    "extern void *smith_objc_global_isa __asm__(\"__NSConcreteGlobalBlock\");\n"
    "extern void *smith_objc_allocate_pair(void *, const char *, unsigned long) __asm__(\"_objc_allocateClassPair\");\n"
    "extern void smith_objc_register_pair(void *) __asm__(\"_objc_registerClassPair\");\n"
    "extern _Bool smith_objc_add_ivar(void *, const char *, unsigned long, unsigned char, const char *)\n"
    "    __asm__(\"_class_addIvar\");\n"
    "extern void *smith_objc_ivar_named(void *, const char *) __asm__(\"_class_getInstanceVariable\");\n"
    "extern long smith_objc_ivar_offset(void *) __asm__(\"_ivar_getOffset\");\n"
    "extern void *smith_objc_instance_method(void *, smith_objc_sel) __asm__(\"_class_getInstanceMethod\");\n"
    "extern const char *smith_objc_method_types(void *) __asm__(\"_method_getTypeEncoding\");\n"
    "extern _Bool smith_objc_add_method_raw(void *, smith_objc_sel, void *, const char *) __asm__(\"_class_addMethod\");\n"
    "extern void *smith_objc_implementation(void *) __asm__(\"_imp_implementationWithBlock\");\n"
    "extern void *smith_objc_protocol_named(const char *) __asm__(\"_objc_getProtocol\");\n"
    "extern void *smith_objc_open(const char *, int) __asm__(\"_dlopen\");\n"
    "extern _Bool smith_objc_add_protocol(void *, void *) __asm__(\"_class_addProtocol\");\n"
    "struct smith_objc_description { smith_objc_sel name; const char *types; };\n"
    "extern struct smith_objc_description smith_objc_protocol_method(void *, smith_objc_sel, _Bool, _Bool)\n"
    "    __asm__(\"_protocol_getMethodDescription\");\n"
    "extern void *smith_objc_superclass_of(void *) __asm__(\"_class_getSuperclass\");\n"
    "extern void *smith_objc_class_of_object(void *) __asm__(\"_object_getClass\");\n"
    "extern void smith_objc_send_super(void) __asm__(\"_objc_msgSendSuper\");\n"
    "struct smith_objc_super { void *receiver; void *super_class; };\n"
    // Apple's block layout: a header, a descriptor and what the block holds. `objc::Block` is the header with the
    // descriptor inline; a global block is never copied, so the descriptor stays where it is.
    "typedef struct { void *isa; int flags; int reserved; void *invoke; void *descriptor; unsigned long reserved_size;\n"
    "                 unsigned long size; } objc_Block;\n"
    "static inline void smith_objc_block_fill(void *context, unsigned long function, unsigned long size) {\n"
    "    objc_Block *block = (objc_Block *)context;\n"
    "    block->isa = &smith_objc_global_isa;\n"
    "    block->flags = 1 << 28;\n"
    "    block->reserved = 0;\n"
    "    block->invoke = (void *)function;\n"
    "    block->descriptor = &block->reserved_size;\n"
    "    block->reserved_size = 0;\n"
    "    block->size = size;\n"
    "}\n"
    ;

constexpr char smith_lang_objc_class_runtime_more[] =
    "static inline void *objc_alloc(void *created) {\n"
    "    return ((void *(*)(void *, smith_objc_sel))smith_objc_send)(created, smith_objc_selector(\"alloc\"));\n"
    "}\n"
    "struct smith_objc_literal { void *isa; int flags; int reserved; void *invoke; void *descriptor;\n"
    "                            unsigned long slot; unsigned long delta; void *function; };\n"
    "static inline void *smith_objc_class_begin(const char *name, void *super, unsigned long size, unsigned long align) {\n"
    "    unsigned long delta = align > 8 ? align : 8;\n"
    "    void *created = smith_objc_allocate_pair(super, name, 0);\n"
    "    if (created == nullptr) return nullptr;\n"
    "    unsigned char shift = 3;\n"
    "    for (unsigned long a = 8; a < delta; a *= 2) shift += 1;\n"
    "    smith_objc_add_ivar(created, \"smith_data\", delta + size, shift, nullptr);\n"
    "    smith_objc_register_pair(created);\n"
    "    return created;\n"
    "}\n"
    "static inline unsigned long smith_objc_slot(void *created) {\n"
    "    return (unsigned long)smith_objc_ivar_offset(smith_objc_ivar_named(created, \"smith_data\"));\n"
    "}\n"
    "static inline void *smith_objc_object_of(void *data, unsigned long align) {\n"
    "    unsigned long delta = align > 8 ? align : 8;\n"
    "    return *(void **)((char *)data - delta);\n"
    "}\n"
    "static inline void *smith_objc_data_of(void *object, unsigned long align) {\n"
    "    unsigned long delta = align > 8 ? align : 8;\n"
    "    void *variable = smith_objc_ivar_named(smith_objc_class_of_object(object), \"smith_data\");\n"
    "    return variable == nullptr ? nullptr : (char *)object + smith_objc_ivar_offset(variable) + delta;\n"
    "}\n"
    "static inline void smith_objc_add(void *created, const char *selector, void *function, void *adapter,\n"
    "                                  unsigned long slot, unsigned long delta, void *protocol) {\n"
    "    smith_objc_sel sel = smith_objc_selector(selector);\n"
    "    const char *types = nullptr;\n"
    "    void *method = smith_objc_instance_method(smith_objc_superclass_of(created), sel);\n"
    "    if (method != nullptr) types = smith_objc_method_types(method);\n"
    "    if (types == nullptr && protocol != nullptr) {\n"
    "        struct smith_objc_description found = smith_objc_protocol_method(protocol, sel, 1, 1);\n"
    "        if (found.types == nullptr) found = smith_objc_protocol_method(protocol, sel, 0, 1);\n"
    "        types = found.types;\n"
    "    }\n"
    "    if (types == nullptr) __builtin_trap();\n" // a method nothing declares: the table named a selector it cannot have
    "    static const struct { unsigned long reserved; unsigned long size; } descriptor = {\n"
    "        0, sizeof(struct smith_objc_literal)};\n"
    "    struct smith_objc_literal literal = {&smith_objc_stack_isa, 0, 0, adapter, (void *)&descriptor, slot, delta,\n"
    "                                         function};\n"
    "    smith_objc_add_method_raw(created, sel, smith_objc_implementation(&literal), types);\n"
    "}\n";

// ---- The text of the file's uses of objc::methods

static bool smith_lang_objc_class_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static const char *smith_lang_objc_class_skip(const char *at, const char *end) {
    while (at < end && smith_lang_objc_class_space(*at))
        at += 1;
    return at;
}

// A word at `at`, copied into `out`; false when there is none or it is too long.
static bool smith_lang_objc_class_word(const char **at, const char *end, char *out, size_t capacity) {
    const char *start = *at;
    size_t size = 0;
    while (start + size < end && smith_lang_objc_word(start[size]))
        size += 1;
    if (size == 0 || size + 1 > capacity) return false;
    memcpy(out, start, size);
    out[size] = 0;
    *at = start + size;
    return true;
}

static bool smith_lang_objc_class_expect(const char **at, const char *end, const char *text) {
    const char *cursor = smith_lang_objc_class_skip(*at, end);
    size_t size = strlen(text);
    if ((size_t)(end - cursor) < size || memcmp(cursor, text, size) != 0) return false;
    *at = cursor + size;
    return true;
}

// The pairs `objc::methods(objc::P, D)` the text writes, once each.
static size_t smith_lang_objc_class_uses(const SmithLang *lang, const SmithHost *host, SmithLangObjcUse *uses,
                                         size_t capacity) {
    size_t count = 0;
    const char *text = host->main_text;
    const char *end = text != nullptr ? text + host->main_text_length : nullptr;
    static const char marker[] = "objc::methods";
    if (text == nullptr) return 0;
    for (const char *found = memmem(text, (size_t)(end - text), marker, sizeof marker - 1);
         found != nullptr && count < capacity;
         found = memmem(found + 1, (size_t)(end - found - 1), marker, sizeof marker - 1)) { // one pass for each use
        const char *at = found + sizeof marker - 1;
        SmithLangObjcUse use = {};
        if (!smith_lang_objc_class_expect(&at, end, "(") || !smith_lang_objc_class_expect(&at, end, "objc") ||
            !smith_lang_objc_class_expect(&at, end, "::"))
            continue;
        at = smith_lang_objc_class_skip(at, end);
        if (!smith_lang_objc_class_word(&at, end, use.owner, sizeof use.owner) ||
            !smith_lang_objc_class_expect(&at, end, ","))
            continue;
        at = smith_lang_objc_class_skip(at, end);
        if (!smith_lang_objc_class_word(&at, end, use.data, sizeof use.data) ||
            !smith_lang_objc_class_expect(&at, end, ")"))
            continue;
        SmithLangObjcSpan spans[1];
        size_t owner_length = strlen(use.owner);
        if (smith_lang_objc_blocks(lang, 'c', use.owner, owner_length, spans, 1) > 0)
            use.kind = 'c';
        else if (smith_lang_objc_blocks(lang, 'p', use.owner, owner_length, spans, 1) > 0)
            use.kind = 'p';
        else
            continue;
        bool known = false;
        for (size_t i = 0; i < count; i++)
            known = known || (strcmp(uses[i].owner, use.owner) == 0 && strcmp(uses[i].data, use.data) == 0);
        if (!known) uses[count++] = use;
    }
    return count;
}

// ---- The methods of a class or protocol that a table has a field for

static bool smith_lang_objc_class_keyword(const char *name, size_t length) {
    static const char *const words[] = {
        "auto",     "break",  "case",     "char",     "const",    "continue", "default",       "do",
        "double",   "else",   "enum",     "extern",   "float",    "for",      "goto",          "if",
        "inline",   "int",    "long",     "register", "restrict", "return",   "short",         "signed",
        "sizeof",   "static", "struct",   "switch",   "typedef",  "union",    "unsigned",      "void",
        "volatile", "while",  "bool",     "true",     "false",    "nullptr",  "constexpr",     "typeof",
        "alignas",  "alignof", "static_assert", "thread_local"};
    for (size_t i = 0; i < sizeof words / sizeof words[0]; i++) { // 44 words
        if (strlen(words[i]) == length && memcmp(words[i], name, length) == 0) return true;
    }
    return false;
}

// The instance methods of the owner and of everything it inherits, first one of a name: `smith_lang_objc_fields`.
static size_t smith_lang_objc_class_collect(const SmithLang *lang, char kind, const char *name, size_t length) {
    static SmithLangObjcChain chain;
    smith_lang_objc_chain(lang, kind, name, length, &chain);
    memset(smith_lang_objc_field_set, 0, sizeof smith_lang_objc_field_set);
    size_t count = 0;
    for (size_t o = 0; o < chain.count; o++) {
        SmithLangObjcSpan spans[8];
        size_t blocks = smith_lang_objc_blocks(lang, chain.owners[o].kind, chain.owners[o].name,
                                               strlen(chain.owners[o].name), spans, 8);
        for (size_t s = 0; s < blocks; s++) {
            const char *line = spans[s].start;
            for (size_t guard = 0; line < spans[s].end && guard < 1000000; guard++) { // by line
                const char *newline = memchr(line, '\n', (size_t)(spans[s].end - line));
                const char *end = newline != nullptr ? newline : spans[s].end;
                SmithLangObjcMethod method;
                if (*line == 'm' && smith_lang_objc_method(line, end, &method) && method.kind[0] == '-' &&
                    !smith_lang_objc_class_keyword(method.name, method.name_length) &&
                    !smith_lang_objc_set_add(smith_lang_objc_field_set, method.name, method.name_length) &&
                    count < smith_lang_objc_fields_max)
                    smith_lang_objc_fields[count++] = method;
                if (newline == nullptr) break;
                line = newline + 1;
            }
        }
    }
    return count;
}

// ---- The C text for a pair

static void smith_lang_objc_class_printf(SmithLangObjcOut *out, const char *format, ...) {
    char buffer[1024];
    va_list arguments;
    va_start(arguments, format);
    int size = vsnprintf(buffer, sizeof buffer, format, arguments);
    va_end(arguments);
    if (size > 0 && (size_t)size < sizeof buffer) smith_lang_objc_put(out, buffer, (size_t)size);
}

static bool smith_lang_objc_class_is_void(const SmithLangObjcMethod *method) {
    return method->type_lengths[0] == 4 && memcmp(method->types[0], "void", 4) == 0;
}

// A parameter list `T1 a1, T2 a2` of the method, with `first` before it.
static void smith_lang_objc_class_params(SmithLangObjcOut *out, const SmithLangObjcMethod *method, const char *owner,
                                         size_t owner_length, bool named) {
    char number[16];
    for (size_t i = 1; i < method->type_count; i++) {
        smith_lang_objc_puts(out, ", ");
        smith_lang_objc_type(out, method->types[i], method->type_lengths[i], owner, owner_length);
        if (named) {
            snprintf(number, sizeof number, " a%zu", i);
            smith_lang_objc_puts(out, number);
        }
    }
}

// The adapter of field `index` of the owner: the function an Objective-C call reaches, which finds the data in the object
// and calls the table's function with it.
static void smith_lang_objc_class_adapter(SmithLangObjcOut *out, const SmithLangObjcUse *use, const char *data,
                                          size_t index, const SmithLangObjcMethod *method) {
    const char *owner = use->owner;
    size_t owner_length = strlen(owner);
    bool is_void = smith_lang_objc_class_is_void(method);
    smith_lang_objc_puts(out, "static ");
    smith_lang_objc_type(out, method->types[0], method->type_lengths[0], owner, owner_length);
    smith_lang_objc_class_printf(out, " smith_objc_adapter_%s_%s_%zu(struct smith_objc_literal *block, void *self",
                                 owner, use->data, index);
    smith_lang_objc_class_params(out, method, owner, owner_length, true);
    smith_lang_objc_puts(out, ") {\n    *(void **)((char *)self + block->slot) = self;\n    ");
    smith_lang_objc_puts(out, is_void ? "((" : "return ((");
    smith_lang_objc_type(out, method->types[0], method->type_lengths[0], owner, owner_length);
    smith_lang_objc_class_printf(out, " (*)(%s *", data);
    smith_lang_objc_class_params(out, method, owner, owner_length, false);
    smith_lang_objc_puts(out, "))block->function)((void *)((char *)self + block->slot + block->delta)");
    char number[16];
    for (size_t i = 1; i < method->type_count; i++) {
        snprintf(number, sizeof number, ", a%zu", i);
        smith_lang_objc_puts(out, number);
    }
    smith_lang_objc_puts(out, ");\n}\n");
}

// Everything for the owner P: the adapters and `smith_objc_add_P`, which adds the methods a table filled in.
static void smith_lang_objc_class_owner(const SmithLang *lang, SmithLangObjcOut *out, const SmithLangObjcUse *use,
                                        const char *data) {
    size_t length = strlen(use->owner);
    size_t count = smith_lang_objc_class_collect(lang, use->kind, use->owner, length);
    for (size_t i = 0; i < count; i++)
        smith_lang_objc_class_adapter(out, use, data, i, &smith_lang_objc_fields[i]);
    smith_lang_objc_class_printf(out,
                                 "static void smith_objc_add_%s_%s(void *created, void *const *table, unsigned long slot, "
                                 "unsigned long delta) {\n",
                                 use->owner, use->data);
    if (use->kind == 'p')
    {
        // A framework nothing else refers to is not loaded (the linker drops it), and its protocols are not found.
        smith_lang_objc_class_printf(out, "    void *protocol = smith_objc_protocol_named(\"%.*s\");\n", (int)length,
                                     use->owner);
        for (uint32_t f = 0; f < lang->objc.frameworks_count; f++)
            smith_lang_objc_class_printf(out,
                                         "    if (protocol == nullptr) {\n"
                                         "        smith_objc_open(\"/System/Library/Frameworks/%s.framework/%s\", 1);\n"
                                         "        protocol = smith_objc_protocol_named(\"%.*s\");\n    }\n",
                                         lang->objc.frameworks[f], lang->objc.frameworks[f], (int)length, use->owner);
        smith_lang_objc_class_printf(out, "    if (protocol != nullptr) smith_objc_add_protocol(created, protocol);\n");
    }
    else
        smith_lang_objc_puts(out, "    void *protocol = nullptr;\n");
    for (size_t i = 0; i < count; i++) {
        const SmithLangObjcMethod *method = &smith_lang_objc_fields[i];
        smith_lang_objc_class_printf(out, "    if (table[%zu] != nullptr) smith_objc_add(created, \"", i);
        smith_lang_objc_put(out, method->selector, method->selector_length);
        smith_lang_objc_class_printf(out, "\", table[%zu], (void *)smith_objc_adapter_%s_%s_%zu, slot, delta, protocol);\n",
                                     i, use->owner, use->data, i);
    }
    smith_lang_objc_puts(out, "}\n");
}

// The table struct of the pair and the function that adds its methods; `Data` is declared here as a struct tag, which a
// later `struct Data { ... };` of the file completes.
static void smith_lang_objc_class_pair(SmithLang *lang, const SmithLangObjcUse *use, SmithLangObjcOut *out) {
    size_t length = strlen(use->owner);
    size_t count = smith_lang_objc_class_collect(lang, use->kind, use->owner, length);
    const char *prefixed = smith_lang_prefixed(lang, use->data, strlen(use->data));
    char data[200];
    snprintf(data, sizeof data, "%.*s", (int)lang->tmp_length, prefixed);
    smith_lang_objc_class_printf(out, "struct %s;\ntypedef struct %s %s;\n", data, data, data);
    smith_lang_objc_class_owner(lang, out, use, data);
    smith_lang_objc_class_printf(out, "struct objc_methods_%s_%s {\n", use->owner, use->data);
    if (count == 0) smith_lang_objc_puts(out, "    char none;\n");
    for (size_t i = 0; i < count; i++) {
        const SmithLangObjcMethod *method = &smith_lang_objc_fields[i];
        smith_lang_objc_puts(out, "    ");
        smith_lang_objc_type(out, method->types[0], method->type_lengths[0], use->owner, length);
        smith_lang_objc_class_printf(out, " (*%.*s)(%s *", (int)method->name_length, method->name, data);
        smith_lang_objc_class_params(out, method, use->owner, length, false);
        smith_lang_objc_puts(out, ");\n");
    }
    smith_lang_objc_class_printf(out, "};\ntypedef struct objc_methods_%s_%s objc_methods_%s_%s;\n", use->owner,
                                 use->data, use->owner, use->data);
    smith_lang_objc_class_printf(out,
                                 "static inline __attribute__((overloadable)) void smith_objc_add_methods(void *created, "
                                 "const objc_methods_%s_%s *table, unsigned long slot, unsigned long delta) {\n"
                                 "    smith_objc_add_%s_%s(created, (void *const *)table, slot, delta);\n}\n",
                                 use->owner, use->data, use->owner, use->data);
}

// ---- Super calls: `objc::NSWindow::super_setTitle(w, t)` sends the message to `w`, starting above NSWindow.

static void smith_lang_objc_class_super(SmithLangObjcOut *out, const char *self, size_t self_length,
                                        const SmithLangObjcMethod *method) {
    bool is_void = smith_lang_objc_class_is_void(method);
    smith_lang_objc_puts(out, "static inline ");
    smith_lang_objc_type(out, method->types[0], method->type_lengths[0], self, self_length);
    smith_lang_objc_class_printf(out, " objc_%.*s_super_%.*s(objc_%.*s *self", (int)self_length, self,
                                 (int)method->name_length, method->name, (int)self_length, self);
    smith_lang_objc_class_params(out, method, self, self_length, true);
    smith_lang_objc_class_printf(out,
                                 ") { struct smith_objc_super super = {self, (void *)&smith_objc_class_symbol_%.*s}; ",
                                 (int)self_length, self);
    smith_lang_objc_puts(out, is_void ? "((" : "return ((");
    smith_lang_objc_type(out, method->types[0], method->type_lengths[0], self, self_length);
    smith_lang_objc_puts(out, " (*)(struct smith_objc_super *, smith_objc_sel");
    smith_lang_objc_class_params(out, method, self, self_length, false);
    smith_lang_objc_puts(out, "))smith_objc_send_super)(&super, smith_objc_selector(\"");
    smith_lang_objc_put(out, method->selector, method->selector_length);
    smith_lang_objc_puts(out, "\")");
    char number[16];
    for (size_t i = 1; i < method->type_count; i++) {
        snprintf(number, sizeof number, ", a%zu", i);
        smith_lang_objc_puts(out, number);
    }
    smith_lang_objc_puts(out, "); }\n");
}

// ---- What the last import writes

// Whether the file's text has `word`.
static bool smith_lang_objc_class_mentions(const SmithHost *host, const char *word) {
    return host->main_text != nullptr && memmem(host->main_text, host->main_text_length, word, strlen(word)) != nullptr;
}

static bool smith_lang_objc_class_supers;
static bool smith_lang_objc_class_wanted;
static bool smith_lang_objc_class_wants_super(void) { return smith_lang_objc_class_supers; }

// Before the wrappers: the runtime of classes and blocks (and super calls) when the file's text uses them.
static void smith_lang_objc_class_prepare(const SmithHost *host, SmithLangObjcOut *out) {
    smith_lang_objc_class_supers = smith_lang_objc_class_mentions(host, "::super_");
    smith_lang_objc_class_wanted = smith_lang_objc_class_supers || smith_lang_objc_class_mentions(host, "objc::methods") ||
                                   smith_lang_objc_class_mentions(host, "objc::class_define") ||
                                   smith_lang_objc_class_mentions(host, "objc::block") ||
                                   smith_lang_objc_class_mentions(host, "objc::Block");
    if (!smith_lang_objc_class_wanted) return;
    smith_lang_objc_puts(out, smith_lang_objc_class_runtime);
    smith_lang_objc_puts(out, smith_lang_objc_class_runtime_more);
}

// After the wrappers: for each pair of `objc::methods` its struct. `names` are the classes the file wrote after
// `objc::`; each gets `smith_objc_class_of`, which names its class from a pointer's type.
static void smith_lang_objc_class_define(SmithLang *lang, const SmithHost *host, SmithLangObjcOut *out,
                                         char names[][96], size_t name_count) {
    if (!smith_lang_objc_class_wanted) return;
    for (size_t i = 0; i < name_count; i++) {
        SmithLangObjcSpan spans[1];
        if (smith_lang_objc_blocks(lang, 'c', names[i], strlen(names[i]), spans, 1) == 0) continue;
        smith_lang_objc_class_printf(out,
                                     "static inline __attribute__((overloadable)) void *smith_objc_class_of(objc_%s *pointer) "
                                     "{ (void)pointer; return (void *)&smith_objc_class_symbol_%s; }\n",
                                     names[i], names[i]);
    }
    static SmithLangObjcUse uses[smith_lang_objc_uses_max];
    size_t count = smith_lang_objc_class_uses(lang, host, uses, smith_lang_objc_uses_max);
    for (size_t i = 0; i < count; i++) {
        smith_lang_objc_class_pair(lang, &uses[i], out);
    }
}

// ---- Names: objc::methods(objc::P, D)

// The names of the required methods of P (declared by P itself, not inherited) that the compound literal after the use
// does not set.
static void smith_lang_objc_class_required(SmithLang *lang, const SmithHost *host, const char *owner, size_t length,
                                           char kind) {
    const SmithToken *close = smith_lang_peek(lang, host, 0, false);
    const SmithToken *open = smith_lang_peek(lang, host, 1, false);
    if (kind != 'p' || !smith_lang_punct(close, ")") || !smith_lang_punct(open, "{")) return;
    static char set[64][96];
    size_t set_count = 0;
    int depth = 0;
    for (size_t at = 1; at < 900; at++) { // the initializer's tokens
        const SmithToken *token = smith_lang_peek(lang, host, at, false);
        if (token->kind == SmithTokenKind_Eof) return;
        if (smith_lang_punct(token, "{") || smith_lang_punct(token, "(") || smith_lang_punct(token, "[")) depth += 1;
        if (smith_lang_punct(token, "}") || smith_lang_punct(token, ")") || smith_lang_punct(token, "]")) depth -= 1;
        if (depth == 0) break;
        const SmithToken *name = smith_lang_peek(lang, host, at + 1, false);
        if (depth == 1 && smith_lang_punct(token, ".") && name->kind == SmithTokenKind_Identifier &&
            name->length < sizeof set[0] && set_count < 64) {
            memcpy(set[set_count], name->text, name->length);
            set[set_count++][name->length] = 0;
        }
    }
    SmithLangObjcSpan spans[8];
    size_t blocks = smith_lang_objc_blocks(lang, 'p', owner, length, spans, 8);
    for (size_t s = 0; s < blocks; s++) {
        const char *line = spans[s].start;
        for (size_t guard = 0; line < spans[s].end && guard < 1000000; guard++) { // by line
            const char *newline = memchr(line, '\n', (size_t)(spans[s].end - line));
            const char *end = newline != nullptr ? newline : spans[s].end;
            SmithLangObjcMethod method;
            if (*line == 'm' && smith_lang_objc_method(line, end, &method) && method.kind[0] == '-' &&
                memchr(method.flags, 'r', method.flags_length) != nullptr) {
                bool given = false;
                for (size_t i = 0; i < set_count; i++)
                    given = given || (strlen(set[i]) == method.name_length && memcmp(set[i], method.name, method.name_length) == 0);
                if (!given) {
                    smith_lang_error(lang, host, open->location,
                                     "`objc::methods(objc::%.*s, ...)` does not set `.%.*s`, a method that %.*s requires",
                                     (int)length, owner, (int)method.name_length, method.name, (int)length, owner);
                    return;
                }
            }
            if (newline == nullptr) break;
            line = newline + 1;
        }
    }
}

// `objc::methods(objc::P, D)` is the struct `objc_methods_P_D`. False when it is not that form.
static bool smith_lang_objc_class_methods(SmithLang *lang, const SmithHost *host, const SmithToken *name,
                                          const SmithToken *taken, SmithToken *result) {
    const SmithToken *open = smith_lang_peek(lang, host, 0, false);
    const SmithToken *inner = smith_lang_peek(lang, host, 1, false);
    const SmithToken *colons = smith_lang_peek(lang, host, 2, false);
    const SmithToken *owner = smith_lang_peek(lang, host, 3, false);
    const SmithToken *comma = smith_lang_peek(lang, host, 4, false);
    const SmithToken *data = smith_lang_peek(lang, host, 5, false);
    const SmithToken *close = smith_lang_peek(lang, host, 6, false);
    if (!smith_lang_punct(open, "(") || inner->kind != SmithTokenKind_Identifier ||
        !smith_lang_equals(inner->text, inner->length, "objc") || !smith_lang_punct(colons, "::") ||
        owner->kind != SmithTokenKind_Identifier || !smith_lang_punct(comma, ",") ||
        data->kind != SmithTokenKind_Identifier || !smith_lang_punct(close, ")"))
        return false;
    static const char prefix[] = "objc_methods_";
    if (sizeof prefix - 1 + owner->length + 1 + data->length + 1 > sizeof lang->tmp) {
        lang->broken = true;
        return false;
    }
    char owner_name[96];
    if (owner->length >= sizeof owner_name) return false;
    memcpy(owner_name, owner->text, owner->length);
    owner_name[owner->length] = 0;
    size_t total = sizeof prefix - 1;
    memcpy(lang->tmp, prefix, total);
    memcpy(lang->tmp + total, owner->text, owner->length);
    total += owner->length;
    lang->tmp[total++] = '_';
    memcpy(lang->tmp + total, data->text, data->length);
    total += data->length;
    lang->tmp[total] = 0;
    for (int i = 0; i < 7; i++)
        smith_lang_take(lang, host);
    SmithLangObjcSpan spans[1];
    char kind = smith_lang_objc_blocks(lang, 'p', owner_name, strlen(owner_name), spans, 1) > 0 ? 'p' : 'c';
    smith_lang_objc_class_required(lang, host, owner_name, strlen(owner_name), kind);
    *result = smith_lang_made(name, lang->tmp, total, false);
    result->location = taken->location;
    return true;
}
