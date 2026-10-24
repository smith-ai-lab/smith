// Writes C on <smith/json.h> from a JSON Schema: a struct for every definition, and a parse, a write, an equality and a
// random-value function for each, plus a test file that round-trips every type and refuses every missing required
// member. knowledge/protocol-generation.md has the design; runtime/protocol/protocol.h has what the output stands on.
//
// Usage: protocol NAME SCHEMA LICENSE SOURCE OUT_DIR TEST_DIR [--check]
//   NAME     dap or mcp: the prefix of every name (SmithDap..., smith_dap_...) and of the files
//   SCHEMA   the JSON Schema file; LICENSE its license file (kept beside the output); SOURCE a line saying where the
//            schema came from, put in the header of every generated file
//   OUT_DIR  receives NAME.h, NAME.c and LICENSE-NAME.txt; TEST_DIR receives test_protocol_NAME.c
//   --check  writes nothing: exits 1 if any of those files differs from what would be written
//
// A schema is read into a table of types in four steps, none of which recurses (a schema nests as deep as its author
// likes, and nothing in smith recurses): every node that needs a type gets a slot and is built from a worklist; equal
// types are merged; a reference that closes a cycle is cut (that field is kept as raw JSON, so the generated code
// recurses nowhere either); and the types are put in dependency order.
#include <smith/arena.h>
#include <smith/json.h>
#include <smith/text.h>

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

constexpr size_t gen_types_max = 4096;
constexpr size_t gen_fields_max = 128;
constexpr size_t gen_alts_max = 64;
constexpr size_t gen_values_max = 256;
constexpr size_t gen_stack_max = 64;
constexpr size_t gen_names_max = 8;
constexpr size_t gen_arena_capacity = 96U << 20;
constexpr size_t gen_input_capacity = 8U << 20;
constexpr size_t gen_sink_capacity = 24U << 20;

typedef enum {
    GenKind_Boolean,
    GenKind_Integer,
    GenKind_Number,
    GenKind_String,
    GenKind_Any,
    GenKind_Object,
    GenKind_Enum,
    GenKind_OpenEnum,
    GenKind_ConstInteger,
    GenKind_Struct,
    GenKind_Array,
    GenKind_Map,
    GenKind_Nullable,
    GenKind_Union,
    GenKind_Pending, // a slot whose node has not been read yet
} GenKind;

// The first six kinds are the types every schema starts from: their type ids are the kinds, and they have no slot.
constexpr int gen_primitives = 6;

typedef struct {
    const char *json;  // the member's name in the schema
    const char *ident; // as a C member
    int type;
    bool required;
} GenField;

typedef struct {
    GenKind kind;
    const char *name;  // the C type
    const char *label; // the schema's name for it, in error messages
    const char *stem;  // the C functions' name, to which _parse, _write, _equal and _random are added
    const SmithJsonValue *node;
    const char *hint;
    bool is_definition;
    // Enum and OpenEnum
    const char *const *values;
    size_t value_count;
    int64_t constant; // ConstInteger
    // Struct
    GenField *fields;
    size_t field_count;
    bool extras; // the schema allows members it does not list
    // Array, Map and Nullable: the element. Union: the alternatives.
    int element;
    int *alts;
    size_t alt_count;
    const char **alt_tags;    // Pascal case, for the kind enum
    const char **alt_members; // snake case, for the union
    size_t *attempt;          // the order the alternatives are tried in, when there is no tag member
    const char *tag;          // the member that tells the alternatives apart, or nullptr
    bool ambiguous; // some document is taken by two alternatives of a union in it: what is read may not be what was
                    // written
    int visit;
} GenType;

typedef struct {
    SmithArena arena;
    const char *schema;      // dap
    const char *prefix;      // SmithDap
    const char *stem_prefix; // smith_dap_
    const SmithJsonValue *definitions;
    int *definition_type;
    GenType types[gen_types_max];
    int redirect[gen_types_max];
    size_t type_count;
    int order[gen_types_max];
    size_t order_count;
    char ring[gen_names_max][192];
    size_t ring_next;
} Generator;

typedef struct {
    char *begin;
    size_t length;
} Sink;

alignas(smith_arena_align_max) static unsigned char gen_memory[gen_arena_capacity];
static Generator generator;

[[noreturn]] [[gnu::format(printf, 1, 2)]] static void gen_die(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    fputs("protocol: ", stderr);
    vfprintf(stderr, format, arguments);
    fputc('\n', stderr);
    va_end(arguments);
    exit(1);
}

#define GEN_ASSERT(condition) \
    do { \
        if (!(condition)) gen_die("%s failed at %s:%d", #condition, __FILE__, __LINE__); \
    } while (0)

// ---------------------------------------------------------------------------------------------------------------
// Strings

static const char *gen_copy(Generator *g, const char *bytes, size_t length) {
    char *copy = smith_arena_push_array(&g->arena, 1, length + 1, 1);
    memcpy(copy, bytes, length);
    return copy;
}

static const char **gen_string_block(Generator *g, size_t count) {
    return (const char **)smith_arena_push_array(&g->arena, sizeof(const char *), count, alignof(const char *));
}

static const char *gen_text(Generator *g, struct smith_array_const_char_n text) {
    return gen_copy(g, text.data, text.count[0]);
}

[[gnu::format(printf, 2, 3)]] static const char *gen_format(Generator *g, const char *format, ...) {
    char buffer[1024];
    va_list arguments;
    va_start(arguments, format);
    int written = vsnprintf(buffer, sizeof buffer, format, arguments);
    va_end(arguments);
    GEN_ASSERT(written >= 0 && (size_t)written < sizeof buffer);
    return gen_copy(g, buffer, (size_t)written);
}

// "innerException" is "InnerException", "_meta" is "Meta", "function breakpoint" is "FunctionBreakpoint".
static const char *gen_pascal(Generator *g, const char *text) {
    char out[256];
    size_t length = 0;
    bool upper = true;
    for (size_t i = 0; text[i] != '\0' && length < sizeof out - 2; i++) { // bounded by the buffer
        unsigned char c = (unsigned char)text[i];
        if (!isalnum(c)) {
            upper = true;
            continue;
        }
        out[length++] = (char)(upper ? toupper(c) : c);
        upper = false;
    }
    if (length == 0) return "Empty";
    out[length] = '\0';
    if (isdigit((unsigned char)out[0])) return gen_format(g, "N%s", out);
    return gen_copy(g, out, length);
}

// "SmithDapStoppedEvent" is "smith_dap_stopped_event", "JSONRPCRequest" is "jsonrpc_request".
static const char *gen_snake(Generator *g, const char *name) {
    char out[256];
    size_t length = 0;
    for (size_t i = 0; name[i] != '\0' && length < sizeof out - 2; i++) { // bounded by the buffer
        unsigned char c = (unsigned char)name[i];
        if (!isalnum(c)) {
            if (length > 0 && out[length - 1] != '_') out[length++] = '_';
            continue;
        }
        if (isupper(c) && i > 0) {
            unsigned char before = (unsigned char)name[i - 1];
            unsigned char after = (unsigned char)name[i + 1];
            bool boundary = islower(before) || isdigit(before) || (isupper(before) && islower(after));
            if (boundary && length > 0 && out[length - 1] != '_') out[length++] = '_';
        }
        out[length++] = (char)tolower(c);
    }
    return gen_copy(g, out, length);
}

static const char *const gen_keywords[] = {
    "alignas",  "alignof", "auto",     "bool",   "break",    "case",   "char",    "const",        "constexpr",
    "continue", "default", "do",       "double", "else",     "enum",   "extern",  "false",        "float",
    "for",      "goto",    "if",       "inline", "int",      "long",   "nullptr", "register",     "restrict",
    "return",   "short",   "signed",   "sizeof", "static",   "struct", "switch",  "true",         "typedef",
    "typeof",   "union",   "unsigned", "void",   "volatile", "while",  "asm",     "typeof_unqual"};

// A member name of the schema as a C member: the same, with what C does not allow replaced and keywords marked.
static const char *gen_ident(Generator *g, const char *json) {
    char out[256];
    size_t length = 0;
    for (size_t i = 0; json[i] != '\0' && length < sizeof out - 3; i++) { // bounded by the buffer
        unsigned char c = (unsigned char)json[i];
        out[length++] = (char)(isalnum(c) || c == '_' ? c : '_');
    }
    if (length == 0 || isdigit((unsigned char)out[0])) {
        memmove(out + 1, out, length);
        out[0] = '_';
        length++;
    }
    out[length] = '\0';
    for (size_t i = 0; i < sizeof gen_keywords / sizeof gen_keywords[0]; i++) { // bounded by the table
        if (strcmp(out, gen_keywords[i]) == 0) return gen_format(g, "%s_", out);
    }
    return gen_copy(g, out, length);
}

// ---------------------------------------------------------------------------------------------------------------
// The schema, read

static const SmithJsonValue *gen_get(const SmithJsonValue *node, const char *key) {
    if (node == nullptr || node->kind != SmithJsonKind_Object) return nullptr;
    return smith_json_member(node, key);
}

static bool gen_is_string(const SmithJsonValue *value, const char *string) {
    return value != nullptr && value->kind == SmithJsonKind_String && smith_text_is(value->text, string);
}

static bool gen_list_has(const SmithJsonValue *list, const char *string) {
    for (size_t i = 0; i < list->count; i++) { // bounded by the list
        if (gen_is_string(list->items[i], string)) return true;
    }
    return false;
}

// Whether a list of type names is every kind of JSON value, which is "any JSON".
static bool gen_is_any_list(const SmithJsonValue *list) {
    return gen_list_has(list, "object") && gen_list_has(list, "array") && gen_list_has(list, "string") &&
           gen_list_has(list, "boolean") && (gen_list_has(list, "number") || gen_list_has(list, "integer"));
}

static int gen_primitive_named(const SmithJsonValue *type) {
    if (gen_is_string(type, "string")) return GenKind_String;
    if (gen_is_string(type, "integer")) return GenKind_Integer;
    if (gen_is_string(type, "number")) return GenKind_Number;
    if (gen_is_string(type, "boolean")) return GenKind_Boolean;
    if (gen_is_string(type, "object")) return GenKind_Object;
    return -1;
}

static const char *const gen_structural[] = {"$ref",  "allOf", "anyOf",      "oneOf", "enum",
                                             "_enum", "const", "properties", "items"};

// The primitive type a node is, or -1 when it needs a type of its own.
static int gen_node_primitive(const SmithJsonValue *node) {
    if (node->kind == SmithJsonKind_True) return GenKind_Any;
    if (node->kind != SmithJsonKind_Object)
        gen_die("a schema is an object or true, not kind %d at byte %zu", (int)node->kind, node->offset);
    for (size_t i = 0; i < sizeof gen_structural / sizeof gen_structural[0]; i++) { // bounded by the table
        if (gen_get(node, gen_structural[i]) != nullptr) return -1;
    }
    const SmithJsonValue *type = gen_get(node, "type");
    const SmithJsonValue *more = gen_get(node, "additionalProperties");
    if (more != nullptr && more->kind == SmithJsonKind_Object && more->count > 0) return -1;
    if (type == nullptr) return GenKind_Any;
    if (type->kind == SmithJsonKind_Array) return gen_is_any_list(type) ? GenKind_Any : -1;
    int primitive = gen_primitive_named(type);
    if (primitive >= 0) return primitive;
    if (gen_is_string(type, "array")) return -1;
    gen_die("the type at byte %zu is not one this generator knows", type->offset);
}

// ---------------------------------------------------------------------------------------------------------------
// Slots

static bool gen_name_taken(const Generator *g, const char *name) {
    for (size_t i = 0; i < g->type_count; i++) { // bounded by the table
        if (g->types[i].name != nullptr && strcmp(g->types[i].name, name) == 0) return true;
    }
    return false;
}

static const char *gen_claim(Generator *g, const char *base) {
    const char *name = gen_format(g, "%s%s", g->prefix, base);
    for (int n = 2; gen_name_taken(g, name); n++) { // bounded: a table of gen_types_max names has a free one
        GEN_ASSERT(n < 100000);
        name = gen_format(g, "%s%s%d", g->prefix, base, n);
    }
    return name;
}

static int gen_new_type(Generator *g, GenKind kind, const char *hint, const SmithJsonValue *node) {
    GEN_ASSERT(g->type_count < gen_types_max);
    int id = (int)g->type_count++;
    GenType *t = &g->types[id];
    *t = (GenType){.kind = kind, .node = node, .hint = hint, .element = -1};
    if (hint != nullptr) {
        t->name = gen_claim(g, hint);
        t->label = hint;
    }
    g->redirect[id] = id;
    return id;
}

static int gen_definition_index(const Generator *g, struct smith_array_const_char_n name) {
    for (size_t i = 0; i < g->definitions->count; i++) { // bounded by the definitions
        const struct smith_array_const_char_n key = g->definitions->items[i]->key;
        if (key.count[0] == name.count[0] && memcmp(key.data, name.data, name.count[0]) == 0) return (int)i;
    }
    return -1;
}

static int gen_reference(const Generator *g, const SmithJsonValue *node) {
    const SmithJsonValue *ref = gen_get(node, "$ref");
    if (ref->kind != SmithJsonKind_String) gen_die("a $ref at byte %zu is not a string", ref->offset);
    struct smith_array_const_char_n name = ref->text;
    size_t slash = name.count[0];
    while (slash > 0 && name.data[slash - 1] != '/')
        slash--; // bounded by the reference
    name = (struct smith_array_const_char_n){.data = name.data + slash, .count = {name.count[0] - slash}};
    int index = gen_definition_index(g, name);
    if (index < 0) gen_die("the reference at byte %zu names no definition", ref->offset);
    return g->definition_type[index];
}

// The type of a node: a primitive, a definition, or a slot (made now, built when the worklist reaches it).
static int gen_slot_for(Generator *g, const SmithJsonValue *node, const char *hint) {
    if (gen_get(node, "$ref") != nullptr) {
        int id = gen_reference(g, node);
        GEN_ASSERT(id >= 0);
        return id;
    }
    int primitive = gen_node_primitive(node);
    if (primitive >= 0) return primitive;
    for (size_t i = 0; i < g->type_count; i++) { // bounded by the table
        if (g->types[i].node == node) return (int)i;
    }
    return gen_new_type(g, GenKind_Pending, hint, node);
}

// ---------------------------------------------------------------------------------------------------------------
// Building a slot

static const char *gen_hint(Generator *g, const char *base, const char *suffix) {
    return gen_format(g, "%s%s", base, gen_pascal(g, suffix));
}

static void gen_build_array(Generator *g, int id) {
    GenType *t = &g->types[id];
    const SmithJsonValue *items = gen_get(t->node, "items");
    t->kind = GenKind_Array;
    t->element = items != nullptr ? gen_slot_for(g, items, gen_hint(g, t->hint, "Item")) : GenKind_Any;
}

static void gen_build_map(Generator *g, int id) {
    GenType *t = &g->types[id];
    t->kind = GenKind_Map;
    t->element = gen_slot_for(g, gen_get(t->node, "additionalProperties"), gen_hint(g, t->hint, "Value"));
}

static const char **gen_strings(Generator *g, const SmithJsonValue *list, size_t *count) {
    if (list->kind != SmithJsonKind_Array || list->count > gen_values_max)
        gen_die("an enum at byte %zu is not a list", list->offset);
    const char **values = gen_string_block(g, list->count + 1);
    for (size_t i = 0; i < list->count; i++) { // bounded by the list
        if (list->items[i]->kind != SmithJsonKind_String)
            gen_die("only enums of strings are supported (byte %zu)", list->items[i]->offset);
        values[i] = gen_text(g, list->items[i]->text);
    }
    *count = list->count;
    return values;
}

static void gen_build_enum(Generator *g, int id) {
    GenType *t = &g->types[id];
    const SmithJsonValue *closed = gen_get(t->node, "enum");
    const SmithJsonValue *open = gen_get(t->node, "_enum");
    const SmithJsonValue *constant = gen_get(t->node, "const");
    if (constant != nullptr) {
        if (constant->kind == SmithJsonKind_Number) {
            t->kind = GenKind_ConstInteger;
            if (!smith_text_parse_i64(constant->text, &t->constant))
                gen_die("a const at byte %zu is not an integer", constant->offset);
            return;
        }
        const char **one = gen_string_block(g, 1);
        if (constant->kind != SmithJsonKind_String)
            gen_die("a const at byte %zu is not a string or an integer", constant->offset);
        one[0] = gen_text(g, constant->text);
        t->kind = GenKind_Enum;
        t->values = one;
        t->value_count = 1;
        return;
    }
    t->kind = closed != nullptr ? GenKind_Enum : GenKind_OpenEnum;
    t->values = gen_strings(g, closed != nullptr ? closed : open, &t->value_count);
}

// anyOf or oneOf of constants and enums is one enum; of an enum and a plain string, an open one.
static bool gen_union_is_enum(Generator *g, int id, const SmithJsonValue *alts) {
    GenType *t = &g->types[id];
    const char *values[gen_values_max];
    size_t count = 0;
    bool plain_string = false;
    for (size_t i = 0; i < alts->count; i++) { // bounded by the alternatives
        const SmithJsonValue *alt = alts->items[i];
        const SmithJsonValue *list = gen_get(alt, "enum");
        const SmithJsonValue *constant = gen_get(alt, "const");
        if (list == nullptr && constant == nullptr && gen_node_primitive(alt) == GenKind_String &&
            gen_get(alt, "$ref") == nullptr) {
            plain_string = true;
            continue;
        }
        if (constant != nullptr && constant->kind == SmithJsonKind_String && count < gen_values_max) {
            values[count++] = gen_text(g, constant->text);
        } else if (list != nullptr && gen_get(alt, "$ref") == nullptr) {
            size_t more = 0;
            const char **found = gen_strings(g, list, &more);
            for (size_t j = 0; j < more && count < gen_values_max; j++)
                values[count++] = found[j];
        } else {
            return false;
        }
    }
    if (count == 0) return false;
    const char **copy = gen_string_block(g, count);
    for (size_t i = 0; i < count; i++)
        copy[i] = values[i]; // bounded by the values
    t->kind = plain_string ? GenKind_OpenEnum : GenKind_Enum;
    t->values = copy;
    t->value_count = count;
    return true;
}

static void gen_build_union(Generator *g, int id) {
    GenType *t = &g->types[id];
    const SmithJsonValue *alts = gen_get(t->node, "anyOf");
    if (alts == nullptr) alts = gen_get(t->node, "oneOf");
    if (alts->kind != SmithJsonKind_Array || alts->count == 0 || alts->count > gen_alts_max)
        gen_die("the alternatives at byte %zu are not a list of 1 to %zu", alts->offset, gen_alts_max);
    if (gen_union_is_enum(g, id, alts)) return;
    t->kind = GenKind_Union;
    t->alts = smith_arena_push_array(&g->arena, sizeof(int), alts->count, alignof(int));
    t->alt_count = alts->count;
    for (size_t i = 0; i < alts->count; i++) { // bounded by the alternatives
        t->alts[i] = gen_slot_for(g, alts->items[i], gen_format(g, "%s%zu", t->hint, i + 1));
    }
}

// "type": ["string", "null"], ["integer", "string"]: one type with null allowed, or a union of the primitives.
static void gen_build_type_list(Generator *g, int id) {
    GenType *t = &g->types[id];
    const SmithJsonValue *list = gen_get(t->node, "type");
    int found[gen_alts_max];
    size_t count = 0;
    bool nullable = false;
    for (size_t i = 0; i < list->count && count < gen_alts_max; i++) { // bounded by the list
        if (gen_is_string(list->items[i], "null")) {
            nullable = true;
            continue;
        }
        int primitive = gen_primitive_named(list->items[i]);
        if (primitive < 0 && gen_is_string(list->items[i], "array")) primitive = GenKind_Any;
        if (primitive < 0) gen_die("the type list at byte %zu names a type this generator does not know", list->offset);
        found[count++] = primitive;
    }
    GEN_ASSERT(count > 0);
    if (count == 1 && !nullable) {
        g->redirect[id] = found[0];
        return;
    }
    if (count == 1) {
        t->kind = GenKind_Nullable;
        t->element = found[0];
        return;
    }
    if (nullable) gen_die("a nullable union of primitives (byte %zu) is not supported", list->offset);
    t->kind = GenKind_Union;
    t->alts = smith_arena_push_array(&g->arena, sizeof(int), count, alignof(int));
    memcpy(t->alts, found, count * sizeof(int));
    t->alt_count = count;
}

// ---- structs: the members of a definition and of everything it extends, base first

typedef struct {
    const SmithJsonValue *node;
    bool own; // only the node's own properties: its allOf parts were done
} GenFrame;

typedef struct {
    GenField fields[gen_fields_max];
    size_t count;
    const SmithJsonValue *required[gen_fields_max];
    size_t required_count;
    bool extras;
} GenCollect;

static void gen_add_property(Generator *g, GenCollect *collect, const char *owner, struct smith_array_const_char_n name,
                             const SmithJsonValue *schema) {
    const char *json = gen_text(g, name);
    int type = gen_slot_for(g, schema, gen_hint(g, owner, json));
    for (size_t i = 0; i < collect->count; i++) { // bounded by the members so far
        if (strcmp(collect->fields[i].json, json) == 0) {
            collect->fields[i].type = type; // a derived schema overrides its base's member
            return;
        }
    }
    GEN_ASSERT(collect->count < gen_fields_max);
    collect->fields[collect->count++] = (GenField){.json = json, .ident = gen_ident(g, json), .type = type};
}

static void gen_add_own(Generator *g, GenCollect *collect, const char *owner, const SmithJsonValue *node) {
    const SmithJsonValue *properties = gen_get(node, "properties");
    for (size_t i = 0; properties != nullptr && i < properties->count; i++) { // bounded by the properties
        gen_add_property(g, collect, owner, properties->items[i]->key, properties->items[i]);
    }
    const SmithJsonValue *required = gen_get(node, "required");
    for (size_t i = 0; required != nullptr && i < required->count; i++) { // bounded by the list
        GEN_ASSERT(collect->required_count < gen_fields_max);
        collect->required[collect->required_count++] = required->items[i];
    }
    const SmithJsonValue *more = gen_get(node, "additionalProperties");
    // JSON Schema allows members a schema does not list unless it says false, so they are kept (a launch request's
    // adapter-specific attributes, a tool's own input schema) and written back.
    if (more == nullptr || more->kind != SmithJsonKind_False) collect->extras = true;
}

// The definition a $ref names, as the node to read; a primitive is no base.
static const SmithJsonValue *gen_reference_node(const Generator *g, const SmithJsonValue *node) {
    const SmithJsonValue *ref = gen_get(node, "$ref");
    struct smith_array_const_char_n name = ref->text;
    size_t slash = name.count[0];
    while (slash > 0 && name.data[slash - 1] != '/')
        slash--; // bounded by the reference
    int index = gen_definition_index(
        g, (struct smith_array_const_char_n){.data = name.data + slash, .count = {name.count[0] - slash}});
    if (index < 0) gen_die("the reference at byte %zu names no definition", ref->offset);
    return g->definitions->items[index];
}

static void gen_collect(Generator *g, GenCollect *collect, const char *owner, const SmithJsonValue *root) {
    GenFrame stack[gen_stack_max];
    size_t depth = 0;
    stack[depth++] = (GenFrame){root, false};
    // Each frame pushes its allOf parts (and itself, once, for its own properties), so the loop ends when the
    // inheritance does; it is bounded by the stack, which is asserted.
    for (size_t step = 0; depth > 0 && step < 16 * gen_stack_max; step++) {
        GenFrame frame = stack[--depth];
        if (frame.own) {
            gen_add_own(g, collect, owner, frame.node);
            continue;
        }
        if (gen_get(frame.node, "$ref") != nullptr) {
            stack[depth++] = (GenFrame){gen_reference_node(g, frame.node), false};
            continue;
        }
        const SmithJsonValue *parts = gen_get(frame.node, "allOf");
        GEN_ASSERT(depth + 1 + (parts != nullptr ? parts->count : 0) <= gen_stack_max);
        stack[depth++] = (GenFrame){frame.node, true};
        for (size_t i = parts != nullptr ? parts->count : 0; i > 0; i--) { // the first part is read first
            stack[depth++] = (GenFrame){parts->items[i - 1], false};
        }
    }
    GEN_ASSERT(depth == 0);
}

static void gen_build_struct(Generator *g, int id) {
    GenType *t = &g->types[id];
    GenCollect *collect = smith_arena_push_array(&g->arena, sizeof(GenCollect), 1, alignof(GenCollect));
    gen_collect(g, collect, t->hint, t->node);
    for (size_t i = 0; i < collect->required_count; i++) { // bounded by the required list
        for (size_t j = 0; j < collect->count; j++) {      // bounded by the members
            if (gen_is_string(collect->required[i], collect->fields[j].json)) collect->fields[j].required = true;
        }
    }
    t->kind = GenKind_Struct;
    t->fields = smith_arena_push_array(&g->arena, sizeof(GenField), collect->count + 1, alignof(GenField));
    memcpy(t->fields, collect->fields, collect->count * sizeof(GenField));
    t->field_count = collect->count;
    t->extras = collect->extras;
}

static void gen_build(Generator *g, int id) {
    const SmithJsonValue *node = g->types[id].node;
    const SmithJsonValue *type = gen_get(node, "type");
    if (gen_get(node, "allOf") != nullptr || gen_get(node, "properties") != nullptr) {
        gen_build_struct(g, id);
    } else if (gen_get(node, "anyOf") != nullptr || gen_get(node, "oneOf") != nullptr) {
        gen_build_union(g, id);
    } else if (gen_get(node, "enum") != nullptr || gen_get(node, "_enum") != nullptr ||
               gen_get(node, "const") != nullptr) {
        gen_build_enum(g, id);
    } else if (gen_is_string(type, "array")) {
        gen_build_array(g, id);
    } else if (gen_is_string(type, "object")) {
        gen_build_map(g, id);
    } else if (type != nullptr && type->kind == SmithJsonKind_Array) {
        gen_build_type_list(g, id);
    } else {
        gen_die("the schema at byte %zu is of a shape this generator does not know", node->offset);
    }
}

// One slot for each definition (a definition that is a primitive, or a $ref to another, is that type), then every
// slot built; building makes more slots, so the loop runs to the end of a table that grows.
static void gen_read(Generator *g, const SmithJsonValue *definitions) {
    g->definitions = definitions;
    g->definition_type = smith_arena_push_array(&g->arena, sizeof(int), definitions->count + 1, alignof(int));
    for (size_t i = 0; i < definitions->count; i++)
        g->definition_type[i] = -1;
    for (size_t i = 0; i < definitions->count; i++) { // bounded by the definitions
        const SmithJsonValue *node = definitions->items[i];
        if (gen_get(node, "$ref") != nullptr) continue;
        int primitive = gen_node_primitive(node);
        if (primitive >= 0) {
            g->definition_type[i] = primitive;
            continue;
        }
        int id = gen_new_type(g, GenKind_Pending, gen_pascal(g, gen_text(g, definitions->items[i]->key)), node);
        g->types[id].is_definition = true;
        g->definition_type[i] = id;
    }
    for (size_t pass = 0; pass < definitions->count; pass++) { // a chain of references is at most this long
        for (size_t i = 0; i < definitions->count; i++) {      // bounded by the definitions
            if (g->definition_type[i] >= 0) continue;
            int target = gen_reference(g, definitions->items[i]);
            g->definition_type[i] = target;
        }
    }
    for (size_t i = 0; i < definitions->count; i++) { // a reference to a reference is left at -1 by the loop above
        if (g->definition_type[i] < 0)
            gen_die("the definition %s is a reference that never ends", gen_text(g, definitions->items[i]->key));
    }
    for (size_t i = gen_primitives; i < g->type_count; i++)
        gen_build(g, (int)i); // type_count grows as slots are built
}

// ---------------------------------------------------------------------------------------------------------------
// Merging equal types

static int gen_final(const Generator *g, int id) {
    for (size_t hops = 0; hops < g->type_count + 1; hops++) { // bounded: a chain is no longer than the table
        if (g->redirect[id] == id) return id;
        id = g->redirect[id];
    }
    gen_die("a redirect loops");
}

static void gen_remap(Generator *g) {
    for (size_t i = 0; i < g->type_count; i++) { // bounded by the table
        GenType *t = &g->types[i];
        if (t->element >= 0) t->element = gen_final(g, t->element);
        for (size_t j = 0; j < t->field_count; j++)
            t->fields[j].type = gen_final(g, t->fields[j].type);
        for (size_t j = 0; j < t->alt_count; j++)
            t->alts[j] = gen_final(g, t->alts[j]);
    }
    for (size_t i = 0; i < g->definitions->count; i++)
        g->definition_type[i] = gen_final(g, g->definition_type[i]);
}

static bool gen_same(const GenType *a, const GenType *b) {
    if (a->kind != b->kind || a->is_definition || b->is_definition) return false;
    switch (a->kind) {
    case GenKind_Array:
    case GenKind_Map:
    case GenKind_Nullable:
        return a->element == b->element;
    case GenKind_Enum:
    case GenKind_OpenEnum:
        if (a->value_count != b->value_count) return false;
        for (size_t i = 0; i < a->value_count; i++) { // bounded by the values
            if (strcmp(a->values[i], b->values[i]) != 0) return false;
        }
        return true;
    case GenKind_ConstInteger:
        return a->constant == b->constant;
    default:
        return false;
    }
}

static void gen_merge(Generator *g) {
    gen_remap(g);
    for (size_t pass = 0; pass < 16; pass++) { // a list of lists merges one level a pass
        bool merged = false;
        for (size_t i = 0; i < g->type_count; i++) { // bounded by the table
            if (g->redirect[i] != (int)i) continue;
            for (size_t j = i + 1; j < g->type_count; j++) { // bounded by the table
                if (g->redirect[j] != (int)j || !gen_same(&g->types[i], &g->types[j])) continue;
                g->redirect[j] = (int)i;
                merged = true;
            }
        }
        gen_remap(g);
        if (!merged) return;
    }
    gen_die("merging types did not settle");
}

// ---------------------------------------------------------------------------------------------------------------
// Cutting cycles and ordering

static bool gen_is_wrapper(const GenType *t) {
    return t->kind == GenKind_Array || t->kind == GenKind_Map || t->kind == GenKind_Nullable;
}

// 0: not a constant member. 1: a string constant (a one-value enum). 2: an integer constant.
static int gen_field_constant(const Generator *g, const GenField *field) {
    if (field->type < gen_primitives) return 0;
    const GenType *t = &g->types[field->type];
    if (t->kind == GenKind_Enum && t->value_count == 1) return 1;
    return t->kind == GenKind_ConstInteger ? 2 : 0;
}

static size_t gen_edge_count(const GenType *t) {
    switch (t->kind) {
    case GenKind_Struct:
        return t->field_count;
    case GenKind_Union:
        return t->alt_count;
    case GenKind_Array:
    case GenKind_Map:
    case GenKind_Nullable:
        return 1;
    default:
        return 0;
    }
}

// The type edge i of t leads to; a constant member leads to nothing.
static int gen_edge_get(const Generator *g, const GenType *t, size_t i) {
    switch (t->kind) {
    case GenKind_Struct:
        return gen_field_constant(g, &t->fields[i]) != 0 ? GenKind_String : t->fields[i].type;
    case GenKind_Union:
        return t->alts[i];
    default:
        return t->element;
    }
}

static void gen_edge_cut(GenType *t, size_t i) {
    switch (t->kind) {
    case GenKind_Struct:
        t->fields[i].type = GenKind_Any;
        break;
    case GenKind_Union:
        t->alts[i] = GenKind_Any;
        break;
    default:
        t->element = GenKind_Any;
        break;
    }
}

typedef struct {
    int type;
    size_t edge;
} GenVisit;

// A reference to a type still being visited closes a cycle. The member that holds the chain of arrays, maps and
// nullables that leads to it (or that holds it) becomes raw JSON, and the visit drops back to the type with the member.
static size_t gen_cut(Generator *g, GenVisit *stack, size_t depth) {
    size_t owner = depth - 1;
    while (owner > 0 && gen_is_wrapper(&g->types[stack[owner].type]))
        owner--; // bounded by the stack
    GenType *t = &g->types[stack[owner].type];
    gen_edge_cut(t, stack[owner].edge - 1);
    for (size_t k = owner + 1; k < depth; k++)
        g->types[stack[k].type].visit = 0; // these are visited again if used
    return owner + 1;
}

static void gen_visit(Generator *g, int root) {
    GenVisit stack[gen_types_max];
    size_t depth = 0;
    if (g->types[root].visit != 0) return;
    g->types[root].visit = 1;
    stack[depth++] = (GenVisit){root, 0};
    // One step follows an edge or finishes a type: at most one of each for every edge and type of the table.
    for (size_t step = 0; depth > 0 && step < 8 * gen_types_max * gen_types_max; step++) {
        GenVisit *top = &stack[depth - 1];
        GenType *t = &g->types[top->type];
        if (top->edge == gen_edge_count(t)) {
            t->visit = 2;
            g->order[g->order_count++] = top->type;
            depth--;
            continue;
        }
        int target = gen_edge_get(g, t, top->edge++);
        if (target < gen_primitives || g->types[target].visit == 2) continue;
        if (g->types[target].visit == 0) {
            g->types[target].visit = 1;
            GEN_ASSERT(depth < gen_types_max);
            stack[depth++] = (GenVisit){target, 0};
            continue;
        }
        depth = gen_cut(g, stack, depth);
    }
    GEN_ASSERT(depth == 0);
}

// A union with an alternative that is any JSON (a cut cycle makes them: JSONValue is an object of JSONValues, an array
// of JSONValues, or a scalar) takes every value, so it is any JSON.
static bool gen_collapse(Generator *g) {
    bool changed = false;
    for (size_t k = 0; k < g->order_count; k++) { // bounded by the types
        const GenType *t = &g->types[g->order[k]];
        for (size_t j = 0; t->kind == GenKind_Union && j < t->alt_count; j++) { // bounded by the alternatives
            if (t->alts[j] != GenKind_Any) continue;
            g->redirect[g->order[k]] = GenKind_Any;
            changed = true;
            break;
        }
    }
    return changed;
}

// Merge equal types, cut the cycles and order the types; a collapsed union changes what is equal, so until none is.
static void gen_settle(Generator *g) {
    for (size_t round = 0; round < 16; round++) { // each round collapses a union, so a few settle it
        gen_merge(g);
        g->order_count = 0;
        for (size_t i = gen_primitives; i < g->type_count; i++)
            g->types[i].visit = 0;
        for (size_t i = 0; i < g->definitions->count; i++) { // every definition is a root of the order
            if (g->definition_type[i] >= gen_primitives) gen_visit(g, g->definition_type[i]);
        }
        if (!gen_collapse(g)) return;
    }
    gen_die("collapsing unions did not settle");
}

// ---------------------------------------------------------------------------------------------------------------
// Names, tags and the order a union tries its alternatives in

static const char *const gen_primitive_names[] = {"Boolean", "Integer", "Number", "String", "Any", "Object"};
static const char *const gen_primitive_stems[] = {"boolean", "integer", "number", "string", "any", "object"};

// The name of a type without the schema's prefix.
static const char *gen_short(const Generator *g, int id) {
    if (id < gen_primitives) return gen_primitive_names[id];
    return g->types[id].name + strlen(g->prefix);
}

static void gen_name_wrappers(Generator *g) {
    for (size_t k = 0; k < g->order_count; k++) { // elements come before what holds them
        GenType *t = &g->types[g->order[k]];
        const char *suffix = t->kind == GenKind_Array ? "List" : t->kind == GenKind_Map ? "Map" : nullptr;
        if (t->kind == GenKind_Nullable) {
            t->name = gen_claim(g, gen_format(g, "Nullable%s", gen_short(g, t->element)));
        } else if (suffix != nullptr) {
            t->name = gen_claim(g, gen_format(g, "%s%s", gen_short(g, t->element), suffix));
        }
        if (t->label == nullptr) t->label = t->name + strlen(g->prefix);
        t->stem = gen_snake(g, t->name);
    }
}

// The kinds of JSON value a type takes, as runtime/protocol/protocol.h's SmithProtocolKind_ bits.
constexpr unsigned gen_mask_null = 1;
constexpr unsigned gen_mask_bool = 2;
constexpr unsigned gen_mask_number = 4;
constexpr unsigned gen_mask_string = 8;
constexpr unsigned gen_mask_array = 16;
constexpr unsigned gen_mask_object = 32;
constexpr unsigned gen_mask_any = 63;

static unsigned gen_mask(const Generator *g, int id) {
    unsigned null = 0;
    for (size_t hops = 0; hops < g->type_count; hops++) { // a nullable of a nullable ends in a type that is not
        if (id < gen_primitives || g->types[id].kind != GenKind_Nullable) break;
        null = gen_mask_null;
        id = g->types[id].element;
    }
    static const unsigned primitive[] = {gen_mask_bool,   gen_mask_number, gen_mask_number,
                                         gen_mask_string, gen_mask_any,    gen_mask_object};
    if (id < gen_primitives) return null | primitive[id];
    switch (g->types[id].kind) {
    case GenKind_Enum:
    case GenKind_OpenEnum:
        return null | gen_mask_string;
    case GenKind_Struct:
    case GenKind_Map:
        return null | gen_mask_object;
    case GenKind_Array:
        return null | gen_mask_array;
    case GenKind_ConstInteger:
        return null | gen_mask_number;
    default:
        return gen_mask_any;
    }
}

static const char *gen_mask_text(Generator *g, unsigned mask) {
    if (mask == gen_mask_any) return "smith_protocol_kind_any";
    static const char *const names[] = {"null", "bool", "number", "string", "array", "object"};
    char text[256] = "";
    for (unsigned bit = 0; bit < 6; bit++) { // the six kinds
        if ((mask & (1U << bit)) == 0) continue;
        size_t used = strlen(text);
        snprintf(text + used, sizeof text - used, "%ssmith_protocol_kind_%s", used > 0 ? " | " : "", names[bit]);
    }
    return gen_copy(g, text, strlen(text));
}

static size_t gen_required_count(const GenType *t) {
    size_t count = 0;
    for (size_t i = 0; t->kind == GenKind_Struct && i < t->field_count; i++) { // bounded by the members
        if (t->fields[i].required) count++;
    }
    return count;
}

// The member (a string constant) of struct `t` named `json`, or nullptr.
static const GenField *gen_constant_member(const Generator *g, const GenType *t, const char *json) {
    for (size_t i = 0; i < t->field_count; i++) { // bounded by the members
        if (strcmp(t->fields[i].json, json) == 0 && gen_field_constant(g, &t->fields[i]) == 1) return &t->fields[i];
    }
    return nullptr;
}

static const char *gen_constant_value(const Generator *g, const GenField *field) {
    return g->types[field->type].values[0];
}

// Alternatives that are all structs with a string constant of one name, each its own, are told apart by it.
static const char *gen_find_tag(const Generator *g, const GenType *t) {
    for (size_t i = 0; i < t->alt_count; i++) {
        if (t->alts[i] < gen_primitives || g->types[t->alts[i]].kind != GenKind_Struct) return nullptr;
    }
    const GenType *first = &g->types[t->alts[0]];
    for (size_t f = 0; t->alt_count > 1 && f < first->field_count; f++) { // bounded by the first alternative's members
        if (gen_field_constant(g, &first->fields[f]) != 1) continue;
        const char *json = first->fields[f].json;
        bool tells = true;
        for (size_t i = 0; i < t->alt_count && tells; i++) { // bounded by the alternatives
            const GenField *a = gen_constant_member(g, &g->types[t->alts[i]], json);
            tells = a != nullptr;
            for (size_t j = 0; tells && j < i; j++) { // bounded by the alternatives before
                const GenField *b = gen_constant_member(g, &g->types[t->alts[j]], json);
                tells = b != nullptr && strcmp(gen_constant_value(g, a), gen_constant_value(g, b)) != 0;
            }
        }
        if (tells) return json;
    }
    return nullptr;
}

// Alternatives are tried most specific first: the one with the most required members, any-JSON ones last.
static bool gen_tried_before(const Generator *g, const GenType *t, size_t a, size_t b) {
    bool any_a = gen_mask(g, t->alts[a]) == gen_mask_any;
    bool any_b = gen_mask(g, t->alts[b]) == gen_mask_any;
    if (any_a != any_b) return any_b;
    size_t need_a = t->alts[a] < gen_primitives ? 0 : gen_required_count(&g->types[t->alts[a]]);
    size_t need_b = t->alts[b] < gen_primitives ? 0 : gen_required_count(&g->types[t->alts[b]]);
    return need_a > need_b;
}

static void gen_finish_union(Generator *g, int id) {
    GenType *t = &g->types[id];
    t->alt_tags = gen_string_block(g, t->alt_count);
    t->alt_members = gen_string_block(g, t->alt_count);
    t->attempt = smith_arena_push_array(&g->arena, sizeof(size_t), t->alt_count, alignof(size_t));
    for (size_t i = 0; i < t->alt_count; i++) { // bounded by the alternatives
        const char *tag = gen_pascal(g, gen_short(g, t->alts[i]));
        for (size_t j = 0; j < i; j++) { // a second alternative of the same name is numbered
            if (strcmp(t->alt_tags[j], tag) == 0) tag = gen_format(g, "%s%zu", tag, i + 1);
        }
        t->alt_tags[i] = tag;
        t->alt_members[i] = gen_ident(g, gen_snake(g, tag));
        size_t at = i; // insertion sort, stable
        while (at > 0 && gen_tried_before(g, t, i, t->attempt[at - 1])) {
            t->attempt[at] = t->attempt[at - 1];
            at--;
        }
        t->attempt[at] = i;
    }
    t->tag = gen_find_tag(g, t);
}

// Whether every document of type y is refused by type x: their JSON kinds differ, or x requires a member y has none of.
static bool gen_rejects(const Generator *g, int x, int y) {
    if ((gen_mask(g, x) & gen_mask(g, y)) == 0) return true;
    if (x < gen_primitives || y < gen_primitives) return false;
    const GenType *a = &g->types[x];
    const GenType *b = &g->types[y];
    if (a->kind != GenKind_Struct || b->kind != GenKind_Struct) return false;
    for (size_t i = 0; i < a->field_count; i++) { // bounded by the members
        if (!a->fields[i].required) continue;
        bool shared = false;
        for (size_t j = 0; j < b->field_count; j++)
            shared = shared || strcmp(a->fields[i].json, b->fields[j].json) == 0; // bounded
        if (!shared) return true;
    }
    return false;
}

// A union that is tried alternative by alternative, with one that takes a document an earlier one does, reads some
// documents as another alternative than the one written. So does a type that holds one.
static void gen_mark_ambiguous(Generator *g) {
    for (size_t k = 0; k < g->order_count; k++) { // the types a type holds come before it
        GenType *t = &g->types[g->order[k]];
        for (size_t i = 0; i < gen_edge_count(t); i++) { // bounded by the edges
            int target = gen_edge_get(g, t, i);
            if (target >= gen_primitives && g->types[target].ambiguous) t->ambiguous = true;
        }
        if (t->kind != GenKind_Union || t->tag != nullptr) continue;
        for (size_t a = 0; a < t->alt_count; a++) {         // bounded by the alternatives
            for (size_t b = a + 1; b < t->alt_count; b++) { // bounded by the alternatives
                if (!gen_rejects(g, t->alts[t->attempt[a]], t->alts[t->attempt[b]])) t->ambiguous = true;
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Emitting

[[gnu::format(printf, 2, 3)]] static void gen_emit(Sink *s, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int written = vsnprintf(s->begin + s->length, gen_sink_capacity - s->length, format, arguments);
    va_end(arguments);
    GEN_ASSERT(written >= 0 && (size_t)written < gen_sink_capacity - s->length);
    s->length += (size_t)written;
}

static void gen_literal(Sink *s, const char *text) {
    gen_emit(s, "\"");
    for (const char *c = text; *c != '\0'; c++) { // bounded by the string
        unsigned char u = (unsigned char)*c;
        if (u == '"' || u == '\\')
            gen_emit(s, "\\%c", *c);
        else if (u < 0x20 || u >= 0x7f)
            gen_emit(s, "\\%03o", u);
        else
            gen_emit(s, "%c", *c);
    }
    gen_emit(s, "\"");
}

static const char *gen_ctype(const Generator *g, int id) {
    static const char *const primitive[] = {
        "bool", "int64_t", "double", "struct smith_array_const_char_n", "SmithProtocolAny", "SmithProtocolAny"};
    return id < gen_primitives ? primitive[id] : g->types[id].name;
}

static const char *gen_fn(Generator *g, int id, const char *verb) {
    char *slot = g->ring[g->ring_next++ % gen_names_max];
    if (id < gen_primitives)
        snprintf(slot, sizeof g->ring[0], "smith_protocol_%s_%s", gen_primitive_stems[id], verb);
    else
        snprintf(slot, sizeof g->ring[0], "%s_%s", g->types[id].stem, verb);
    return slot;
}

// The head of one of a type's four functions, as a prototype (header) or the start of the definition (source).
static void gen_head(Generator *g, Sink *s, int id, const char *verb, bool definition) {
    const char *c = gen_ctype(g, id);
    const char *m = definition ? "[[maybe_unused]] " : "";
    const char *n = definition ? "" : "[[nodiscard]] ";
    const char *name = gen_fn(g, id, verb);
    if (strcmp(verb, "parse") == 0)
        gen_emit(s, "%sbool %s(%sSmithArena *arena, const SmithJsonValue *value, %s *out, SmithProtocolError *error)",
                 n, name, m, c);
    else if (strcmp(verb, "write") == 0)
        gen_emit(s, "void %s(SmithJsonWriter *writer, %sconst %s *value)", name, m, c);
    else if (strcmp(verb, "equal") == 0)
        gen_emit(s, "%sbool %s(%sconst %s *a, %sconst %s *b)", n, name, m, c, m, c);
    else
        gen_emit(s, "void %s(%sSmithArena *arena, %sSmithKey key, %s *out)", name, m, m, c);
    gen_emit(s, definition ? " {\n" : ";\n");
}

static const char *const gen_verbs[] = {"parse", "write", "equal", "random"};

// ---- declarations

static void gen_decl_struct(Generator *g, Sink *s, int id) {
    const GenType *t = &g->types[id];
    gen_emit(s, "typedef struct %s {\n", t->name);
    size_t members = 0;
    for (size_t i = 0; i < t->field_count; i++) { // bounded by the members
        const GenField *f = &t->fields[i];
        if (gen_field_constant(g, f) != 0) continue;
        if (!f->required) gen_emit(s, "    bool has_%s;\n", f->ident);
        gen_emit(s, "    %s %s;\n", gen_ctype(g, f->type), f->ident);
        members++;
    }
    if (t->extras) {
        gen_emit(s, "    SmithProtocolExtra additionalProperties;\n");
        members++;
    }
    if (members == 0) gen_emit(s, "    char reserved_;\n");
    gen_emit(s, "} %s;\n\n", t->name);
}

static void gen_decl_enum(Generator *g, Sink *s, int id) {
    const GenType *t = &g->types[id];
    bool open = t->kind == GenKind_OpenEnum;
    const char *enum_name = open ? gen_format(g, "%sKnown", t->name) : t->name;
    gen_emit(s, "typedef enum %s {\n", enum_name);
    for (size_t i = 0; i < t->value_count; i++) { // bounded by the values
        const char *tag = gen_pascal(g, t->values[i]);
        for (size_t j = 0; j < i; j++) { // a value that makes a name already used is numbered
            if (strcmp(gen_pascal(g, t->values[j]), tag) == 0) tag = gen_format(g, "%s%zu", tag, i + 1);
        }
        gen_emit(s, "    %s_%s,\n", enum_name, tag);
    }
    if (open) gen_emit(s, "    %s_Other, // a string the schema does not list: see text\n", enum_name);
    gen_emit(s, "} %s;\n\n", enum_name);
    if (open)
        gen_emit(s,
                 "typedef struct %s {\n    %s known;\n    struct smith_array_const_char_n text; // the string as it is "
                 "written\n} %s;\n\n",
                 t->name, enum_name, t->name);
}

static void gen_decl_wrapper(Generator *g, Sink *s, int id) {
    const GenType *t = &g->types[id];
    const char *e = gen_ctype(g, t->element);
    if (t->kind == GenKind_Array)
        gen_emit(s, "typedef struct %s {\n    const %s *items;\n    size_t count;\n} %s;\n\n", t->name, e, t->name);
    else if (t->kind == GenKind_Map)
        gen_emit(s,
                 "typedef struct %s {\n    const struct smith_array_const_char_n *keys;\n    const %s *values;\n    "
                 "size_t count;\n} %s;\n\n",
                 t->name, e, t->name);
    else
        gen_emit(s, "typedef struct %s {\n    bool is_null;\n    %s value; // when is_null is false\n} %s;\n\n",
                 t->name, e, t->name);
}

static void gen_decl_union(Generator *g, Sink *s, int id) {
    const GenType *t = &g->types[id];
    gen_emit(s, "typedef enum %sKind {\n", t->name);
    for (size_t i = 0; i < t->alt_count; i++)
        gen_emit(s, "    %sKind_%s,\n", t->name, t->alt_tags[i]); // bounded
    gen_emit(s, "} %sKind;\n\ntypedef struct %s {\n    %sKind kind;\n    union {\n", t->name, t->name, t->name);
    for (size_t i = 0; i < t->alt_count; i++) { // bounded by the alternatives
        gen_emit(s, "        %s %s;\n", gen_ctype(g, t->alts[i]), t->alt_members[i]);
    }
    gen_emit(s, "    } as;\n} %s;\n\n", t->name);
}

static void gen_declare(Generator *g, Sink *s, int id) {
    switch (g->types[id].kind) {
    case GenKind_Struct:
        gen_decl_struct(g, s, id);
        break;
    case GenKind_Enum:
    case GenKind_OpenEnum:
        gen_decl_enum(g, s, id);
        break;
    case GenKind_Union:
        gen_decl_union(g, s, id);
        break;
    default:
        gen_decl_wrapper(g, s, id);
        break;
    }
}

// ---- structs

static const char *gen_presence(const Generator *g, const GenField *f) {
    bool plain = f->type == GenKind_Any || (f->type >= gen_primitives && g->types[f->type].kind == GenKind_Nullable);
    return plain ? "member != nullptr" : "smith_protocol_present(member)";
}

static void gen_constant_check(Generator *g, Sink *s, const GenField *f) {
    if (gen_field_constant(g, f) == 1) {
        gen_emit(s, "smith_protocol_const_text(member, ");
        gen_literal(s, gen_constant_value(g, f));
        gen_emit(s, ", error)");
    } else {
        gen_emit(s, "smith_protocol_const_integer(member, %lld, error)", (long long)g->types[f->type].constant);
    }
}

static void gen_parse_member(Generator *g, Sink *s, const GenType *t, const GenField *f) {
    gen_emit(s, "    member = smith_json_member(value, \"%s\");\n", f->json);
    int constant = gen_field_constant(g, f);
    if (f->required) {
        gen_emit(s, "    if (member == nullptr) return smith_protocol_missing(error, value, \"%s\", \"%s\");\n",
                 t->label, f->json);
    }
    if (constant != 0) {
        gen_emit(s, "    if (%s", f->required ? "!" : "member != nullptr && !");
        gen_constant_check(g, s, f);
        gen_emit(s, ") return smith_protocol_in(error, \"%s\", \"%s\");\n", t->label, f->json);
    } else if (f->required) {
        gen_emit(s, "    if (!%s(arena, member, &out->%s, error)) return smith_protocol_in(error, \"%s\", \"%s\");\n",
                 gen_fn(g, f->type, "parse"), f->ident, t->label, f->json);
    } else {
        gen_emit(s, "    if (%s) {\n        out->has_%s = true;\n", gen_presence(g, f), f->ident);
        gen_emit(s,
                 "        if (!%s(arena, member, &out->%s, error)) return smith_protocol_in(error, \"%s\", \"%s\");\n  "
                 "  }\n",
                 gen_fn(g, f->type, "parse"), f->ident, t->label, f->json);
    }
}

static void gen_source_struct_parse(Generator *g, Sink *s, int id) {
    const GenType *t = &g->types[id];
    if (t->extras) {
        gen_emit(s, "static const char *const %s_known[] = {", t->stem);
        for (size_t i = 0; i < t->field_count; i++) { // bounded by the members
            gen_emit(s, "%s\"%s\"", i > 0 ? ", " : "", t->fields[i].json);
        }
        gen_emit(s, t->field_count == 0 ? "nullptr};\n\n" : "};\n\n");
    }
    gen_head(g, s, id, "parse", true);
    gen_emit(s, "    [[maybe_unused]] const SmithJsonValue *member;\n");
    gen_emit(s, "    if (!smith_protocol_expect_object(value, error)) return false;\n    *out = (%s){};\n", t->name);
    for (size_t i = 0; i < t->field_count; i++)
        gen_parse_member(g, s, t, &t->fields[i]); // bounded by the members
    if (t->extras) {
        gen_emit(
            s, "    if (!smith_protocol_extra_parse(arena, value, %s_known, %zu, &out->additionalProperties, error))\n",
            t->stem, t->field_count);
        gen_emit(s, "        return smith_protocol_in(error, \"%s\", \"additionalProperties\");\n", t->label);
    }
    gen_emit(s, "    return true;\n}\n\n");
}

static void gen_write_member(Generator *g, Sink *s, const GenField *f) {
    int constant = gen_field_constant(g, f);
    const char *indent = f->required || constant != 0 ? "    " : "        ";
    if (!f->required && constant == 0) gen_emit(s, "    if (value->has_%s) {\n", f->ident);
    gen_emit(s, "%ssmith_protocol_key(writer, \"%s\");\n", indent, f->json);
    if (constant == 1) {
        gen_emit(s, "%ssmith_json_write_string(writer, smith_text_make(", indent);
        gen_literal(s, gen_constant_value(g, f));
        gen_emit(s, "));\n");
    } else if (constant == 2) {
        gen_emit(s, "%ssmith_json_write_i64(writer, %lld);\n", indent, (long long)g->types[f->type].constant);
    } else {
        gen_emit(s, "%s%s(writer, &value->%s);\n", indent, gen_fn(g, f->type, "write"), f->ident);
    }
    if (!f->required && constant == 0) gen_emit(s, "    }\n");
}

static void gen_equal_member(Generator *g, Sink *s, const GenField *f) {
    if (gen_field_constant(g, f) != 0) return;
    if (f->required) {
        gen_emit(s, "    if (!%s(&a->%s, &b->%s)) return false;\n", gen_fn(g, f->type, "equal"), f->ident, f->ident);
        return;
    }
    gen_emit(s, "    if (a->has_%s != b->has_%s) return false;\n", f->ident, f->ident);
    gen_emit(s, "    if (a->has_%s && !%s(&a->%s, &b->%s)) return false;\n", f->ident, gen_fn(g, f->type, "equal"),
             f->ident, f->ident);
}

static void gen_random_member(Generator *g, Sink *s, const GenField *f, size_t index) {
    if (gen_field_constant(g, f) != 0) return;
    if (f->required) {
        gen_emit(s, "    %s(arena, smith_key_derive(key, %zu), &out->%s);\n", gen_fn(g, f->type, "random"),
                 2 * index + 1, f->ident);
        return;
    }
    gen_emit(s, "    if (smith_protocol_coin(smith_key_derive(key, %zu))) {\n        out->has_%s = true;\n", 2 * index,
             f->ident);
    gen_emit(s, "        %s(arena, smith_key_derive(key, %zu), &out->%s);\n    }\n", gen_fn(g, f->type, "random"),
             2 * index + 1, f->ident);
}

static void gen_source_struct(Generator *g, Sink *s, int id) {
    const GenType *t = &g->types[id];
    gen_source_struct_parse(g, s, id);
    gen_head(g, s, id, "write", true);
    gen_emit(s, "    smith_json_write_object_begin(writer);\n");
    for (size_t i = 0; i < t->field_count; i++)
        gen_write_member(g, s, &t->fields[i]); // bounded by the members
    if (t->extras) gen_emit(s, "    smith_protocol_extra_write(writer, &value->additionalProperties);\n");
    gen_emit(s, "    smith_json_write_object_end(writer);\n}\n\n");
    gen_head(g, s, id, "equal", true);
    for (size_t i = 0; i < t->field_count; i++)
        gen_equal_member(g, s, &t->fields[i]); // bounded by the members
    if (t->extras) {
        gen_emit(
            s,
            "    if (!smith_protocol_extra_equal(&a->additionalProperties, &b->additionalProperties)) return false;\n");
    }
    gen_emit(s, "    return true;\n}\n\n");
    gen_head(g, s, id, "random", true);
    gen_emit(s, "    *out = (%s){};\n", t->name);
    for (size_t i = 0; i < t->field_count; i++)
        gen_random_member(g, s, &t->fields[i], i); // bounded by the members
    if (t->extras) {
        gen_emit(s, "    smith_protocol_extra_random(arena, smith_key_derive(key, %zu), &out->additionalProperties);\n",
                 2 * t->field_count);
    }
    gen_emit(s, "}\n\n");
}

// ---- enums

static void gen_source_enum_names(Generator *g, Sink *s, int id) {
    const GenType *t = &g->types[id];
    gen_emit(s, "static const char *const %s_names[] = {", t->stem);
    for (size_t i = 0; i < t->value_count; i++) { // bounded by the values
        gen_emit(s, "%s", i > 0 ? ", " : "");
        gen_literal(s, t->values[i]);
    }
    gen_emit(s, "};\n\n");
}

static void gen_source_enum(Generator *g, Sink *s, int id) {
    const GenType *t = &g->types[id];
    bool open = t->kind == GenKind_OpenEnum;
    gen_source_enum_names(g, s, id);
    gen_head(g, s, id, "parse", true);
    gen_emit(
        s,
        "    size_t index;\n    if (!smith_protocol_enum_lookup(value, %s_names, %zu, &index, error)) return false;\n",
        t->stem, t->value_count);
    if (open) {
        gen_emit(s, "    out->known = (%sKnown)index;\n    out->text = value->text;\n    return true;\n}\n\n", t->name);
    } else {
        gen_emit(s, "    if (index == %zu) return smith_protocol_fail(error, SmithProtocolReason_BadValue, value);\n",
                 t->value_count);
        gen_emit(s, "    *out = (%s)index;\n    return true;\n}\n\n", t->name);
    }
    gen_head(g, s, id, "write", true);
    if (open) {
        gen_emit(s, "    if ((size_t)value->known < %zu) {\n", t->value_count);
        gen_emit(s,
                 "        smith_json_write_string(writer, smith_text_make(%s_names[value->known]));\n        return;\n "
                 "   }\n",
                 t->stem);
        gen_emit(s, "    smith_json_write_string(writer, value->text);\n}\n\n");
    } else {
        gen_emit(s, "    smith_json_write_string(writer, smith_text_make(%s_names[*value]));\n}\n\n", t->stem);
    }
    gen_head(g, s, id, "equal", true);
    if (open)
        gen_emit(s, "    return a->known == b->known && smith_protocol_string_equal(&a->text, &b->text);\n}\n\n");
    else
        gen_emit(s, "    return *a == *b;\n}\n\n");
    gen_head(g, s, id, "random", true);
    if (open) {
        gen_emit(s, "    size_t pick = smith_protocol_pick(smith_key_derive(key, 0), %zu);\n", t->value_count + 1);
        gen_emit(s, "    out->known = (%sKnown)pick;\n", t->name);
        gen_emit(
            s,
            "    out->text = pick < %zu ? smith_text_make(%s_names[pick]) : smith_protocol_name(arena, pick);\n}\n\n",
            t->value_count, t->stem);
    } else {
        gen_emit(s, "    *out = (%s)smith_protocol_pick(key, %zu);\n}\n\n", t->name, t->value_count);
    }
}

// ---- arrays, maps, nullables

static void gen_source_array(Generator *g, Sink *s, int id) {
    const GenType *t = &g->types[id];
    const char *e = gen_ctype(g, t->element);
    gen_head(g, s, id, "parse", true);
    gen_emit(s, "    void *block;\n");
    gen_emit(
        s, "    if (!smith_protocol_array_parse(arena, value, sizeof(%s), alignof(%s), &block, error)) return false;\n",
        e, e);
    gen_emit(s, "    %s *items = block;\n", e);
    gen_emit(s, "    for (size_t i = 0; i < value->count; i++) { // bounded by the array\n");
    gen_emit(s, "        if (!%s(arena, value->items[i], &items[i], error)) return false;\n    }\n",
             gen_fn(g, t->element, "parse"));
    gen_emit(s, "    out->items = items;\n    out->count = value->count;\n    return true;\n}\n\n");
    gen_head(g, s, id, "write", true);
    gen_emit(s, "    smith_json_write_array_begin(writer);\n");
    gen_emit(s, "    for (size_t i = 0; i < value->count; i++) { // bounded by the array\n");
    gen_emit(s, "        %s(writer, &value->items[i]);\n    }\n    smith_json_write_array_end(writer);\n}\n\n",
             gen_fn(g, t->element, "write"));
    gen_head(g, s, id, "equal", true);
    gen_emit(s, "    if (a->count != b->count) return false;\n");
    gen_emit(s, "    for (size_t i = 0; i < a->count; i++) { // bounded by the array\n");
    gen_emit(s, "        if (!%s(&a->items[i], &b->items[i])) return false;\n    }\n    return true;\n}\n\n",
             gen_fn(g, t->element, "equal"));
    gen_head(g, s, id, "random", true);
    gen_emit(s, "    size_t count = smith_protocol_count(smith_key_derive(key, 0));\n");
    gen_emit(s, "    %s *items = smith_protocol_block(arena, sizeof(%s), alignof(%s), count);\n", e, e, e);
    gen_emit(s, "    for (size_t i = 0; i < count; i++) { // bounded by the count\n");
    gen_emit(s, "        %s(arena, smith_key_derive(key, i + 1), &items[i]);\n    }\n",
             gen_fn(g, t->element, "random"));
    gen_emit(s, "    out->items = items;\n    out->count = count;\n}\n\n");
}

static void gen_source_map(Generator *g, Sink *s, int id) {
    const GenType *t = &g->types[id];
    const char *e = gen_ctype(g, t->element);
    gen_head(g, s, id, "parse", true);
    gen_emit(s, "    struct smith_array_const_char_n *keys;\n    void *block;\n");
    gen_emit(s, "    if (!smith_protocol_map_parse(arena, value, sizeof(%s), alignof(%s), &keys, &block, error))\n", e,
             e);
    gen_emit(s, "        return false;\n    %s *items = block;\n", e);
    gen_emit(s, "    for (size_t i = 0; i < value->count; i++) { // bounded by the object\n");
    gen_emit(s, "        if (!%s(arena, value->items[i], &items[i], error)) return false;\n    }\n",
             gen_fn(g, t->element, "parse"));
    gen_emit(
        s, "    out->keys = keys;\n    out->values = items;\n    out->count = value->count;\n    return true;\n}\n\n");
    gen_head(g, s, id, "write", true);
    gen_emit(s, "    smith_json_write_object_begin(writer);\n");
    gen_emit(s, "    for (size_t i = 0; i < value->count; i++) { // bounded by the map\n");
    gen_emit(s,
             "        smith_json_write_key(writer, value->keys[i]);\n        %s(writer, &value->values[i]);\n    }\n",
             gen_fn(g, t->element, "write"));
    gen_emit(s, "    smith_json_write_object_end(writer);\n}\n\n");
    gen_head(g, s, id, "equal", true);
    gen_emit(s, "    if (a->count != b->count) return false;\n");
    gen_emit(s, "    for (size_t i = 0; i < a->count; i++) { // bounded by the map\n");
    gen_emit(s, "        if (!smith_protocol_string_equal(&a->keys[i], &b->keys[i])) return false;\n");
    gen_emit(s, "        if (!%s(&a->values[i], &b->values[i])) return false;\n    }\n    return true;\n}\n\n",
             gen_fn(g, t->element, "equal"));
    gen_head(g, s, id, "random", true);
    gen_emit(s, "    size_t count = smith_protocol_count(smith_key_derive(key, 0));\n");
    gen_emit(s, "    struct smith_array_const_char_n *keys = smith_protocol_block(arena, sizeof(struct "
                "smith_array_const_char_n), alignof(struct smith_array_const_char_n), count);\n");
    gen_emit(s, "    %s *items = smith_protocol_block(arena, sizeof(%s), alignof(%s), count);\n", e, e, e);
    gen_emit(s, "    for (size_t i = 0; i < count; i++) { // bounded by the count\n");
    gen_emit(s, "        keys[i] = smith_protocol_name(arena, i);\n");
    gen_emit(s, "        %s(arena, smith_key_derive(key, i + 1), &items[i]);\n    }\n",
             gen_fn(g, t->element, "random"));
    gen_emit(s, "    out->keys = keys;\n    out->values = items;\n    out->count = count;\n}\n\n");
}

static void gen_source_nullable(Generator *g, Sink *s, int id) {
    const GenType *t = &g->types[id];
    gen_head(g, s, id, "parse", true);
    gen_emit(s, "    *out = (%s){};\n", t->name);
    gen_emit(
        s, "    if (value->kind == SmithJsonKind_Null) {\n        out->is_null = true;\n        return true;\n    }\n");
    gen_emit(s, "    return %s(arena, value, &out->value, error);\n}\n\n", gen_fn(g, t->element, "parse"));
    gen_head(g, s, id, "write", true);
    gen_emit(s,
             "    if (value->is_null)\n        smith_json_write_null(writer);\n    else\n        %s(writer, "
             "&value->value);\n}\n\n",
             gen_fn(g, t->element, "write"));
    gen_head(g, s, id, "equal", true);
    gen_emit(s, "    if (a->is_null != b->is_null) return false;\n");
    gen_emit(s, "    return a->is_null || %s(&a->value, &b->value);\n}\n\n", gen_fn(g, t->element, "equal"));
    gen_head(g, s, id, "random", true);
    gen_emit(s, "    *out = (%s){};\n    out->is_null = smith_protocol_coin(smith_key_derive(key, 0));\n", t->name);
    gen_emit(s, "    if (!out->is_null) %s(arena, smith_key_derive(key, 1), &out->value);\n}\n\n",
             gen_fn(g, t->element, "random"));
}

// ---- unions

static void gen_union_parse_tagged(Generator *g, Sink *s, int id) {
    const GenType *t = &g->types[id];
    gen_emit(s, "    if (!smith_protocol_expect_object(value, error)) return false;\n");
    gen_emit(s, "    const SmithJsonValue *tag = smith_json_member(value, \"%s\");\n", t->tag);
    gen_emit(s, "    if (tag == nullptr) return smith_protocol_missing(error, value, \"%s\", \"%s\");\n", t->label,
             t->tag);
    gen_emit(s, "    if (tag->kind != SmithJsonKind_String) {\n");
    gen_emit(s, "        (void)smith_protocol_fail(error, SmithProtocolReason_WrongType, tag);\n");
    gen_emit(s, "        return smith_protocol_in(error, \"%s\", \"%s\");\n    }\n", t->label, t->tag);
    for (size_t i = 0; i < t->alt_count; i++) { // bounded by the alternatives
        const GenField *f = gen_constant_member(g, &g->types[t->alts[i]], t->tag);
        gen_emit(s, "    if (smith_protocol_text_is(tag->text, ");
        gen_literal(s, gen_constant_value(g, f));
        gen_emit(s, ")) {\n        out->kind = %sKind_%s;\n", t->name, t->alt_tags[i]);
        gen_emit(s, "        return %s(arena, value, &out->as.%s, error);\n    }\n", gen_fn(g, t->alts[i], "parse"),
                 t->alt_members[i]);
    }
    gen_emit(s, "    (void)smith_protocol_fail(error, SmithProtocolReason_BadValue, tag);\n");
    gen_emit(s, "    return smith_protocol_in(error, \"%s\", \"%s\");\n}\n\n", t->label, t->tag);
}

static void gen_union_parse_tried(Generator *g, Sink *s, int id) {
    const GenType *t = &g->types[id];
    gen_emit(s, "    SmithProtocolError attempt = {};\n");
    for (size_t k = 0; k < t->alt_count; k++) { // bounded by the alternatives
        size_t i = t->attempt[k];
        unsigned mask = gen_mask(g, t->alts[i]);
        gen_emit(s, "    if (");
        if (mask != gen_mask_any) gen_emit(s, "smith_protocol_kind_in(value, %s) && ", gen_mask_text(g, mask));
        gen_emit(s, "%s(arena, value, &out->as.%s, &attempt)) {\n", gen_fn(g, t->alts[i], "parse"), t->alt_members[i]);
        gen_emit(s, "        out->kind = %sKind_%s;\n        return true;\n    }\n", t->name, t->alt_tags[i]);
    }
    gen_emit(s, "    return smith_protocol_fail(error, SmithProtocolReason_NoMatch, value);\n}\n\n");
}

static void gen_source_union(Generator *g, Sink *s, int id) {
    const GenType *t = &g->types[id];
    gen_head(g, s, id, "parse", true);
    gen_emit(s, "    *out = (%s){};\n", t->name);
    if (t->tag != nullptr)
        gen_union_parse_tagged(g, s, id);
    else
        gen_union_parse_tried(g, s, id);
    gen_head(g, s, id, "write", true);
    gen_emit(s, "    switch (value->kind) {\n");
    for (size_t i = 0; i < t->alt_count; i++) { // bounded by the alternatives
        gen_emit(s, "    case %sKind_%s:\n        %s(writer, &value->as.%s);\n        break;\n", t->name,
                 t->alt_tags[i], gen_fn(g, t->alts[i], "write"), t->alt_members[i]);
    }
    gen_emit(s, "    }\n}\n\n");
    gen_head(g, s, id, "equal", true);
    gen_emit(s, "    if (a->kind != b->kind) return false;\n    switch (a->kind) {\n");
    for (size_t i = 0; i < t->alt_count; i++) { // bounded by the alternatives
        gen_emit(s, "    case %sKind_%s:\n        return %s(&a->as.%s, &b->as.%s);\n", t->name, t->alt_tags[i],
                 gen_fn(g, t->alts[i], "equal"), t->alt_members[i], t->alt_members[i]);
    }
    gen_emit(s, "    }\n    return false;\n}\n\n");
    gen_head(g, s, id, "random", true);
    gen_emit(s, "    *out = (%s){};\n", t->name);
    gen_emit(s,
             "    out->kind = (%sKind)smith_protocol_pick(smith_key_derive(key, 0), %zu);\n    switch (out->kind) {\n",
             t->name, t->alt_count);
    for (size_t i = 0; i < t->alt_count; i++) { // bounded by the alternatives
        gen_emit(s, "    case %sKind_%s:\n        %s(arena, smith_key_derive(key, 1), &out->as.%s);\n        break;\n",
                 t->name, t->alt_tags[i], gen_fn(g, t->alts[i], "random"), t->alt_members[i]);
    }
    gen_emit(s, "    }\n}\n\n");
}

static void gen_source(Generator *g, Sink *s, int id) {
    switch (g->types[id].kind) {
    case GenKind_Struct:
        gen_source_struct(g, s, id);
        break;
    case GenKind_Enum:
    case GenKind_OpenEnum:
        gen_source_enum(g, s, id);
        break;
    case GenKind_Array:
        gen_source_array(g, s, id);
        break;
    case GenKind_Map:
        gen_source_map(g, s, id);
        break;
    case GenKind_Nullable:
        gen_source_nullable(g, s, id);
        break;
    case GenKind_Union:
        gen_source_union(g, s, id);
        break;
    default:
        gen_die("%s has a kind that has no code", g->types[id].name);
    }
}

// ---- tests

static void gen_test_prologue(Generator *g, Sink *s, int id, const char *what) {
    const GenType *t = &g->types[id];
    gen_emit(s, "void test_%s_%s_%s(SmithKey key) {\n", g->schema, what, t->stem + strlen(g->stem_prefix));
    gen_emit(s, "    SmithArena arena = smith_test_arena(1 << 23);\n    %s value;\n", t->name);
    gen_emit(s, "    %s(&arena, key, &value);\n", gen_fn(g, id, "random"));
    gen_emit(s, "    SmithJsonWriter writer = protocol_test_writer(&arena);\n    %s(&writer, &value);\n",
             gen_fn(g, id, "write"));
}

static void gen_test_round_trip(Generator *g, Sink *s, int id) {
    const GenType *t = &g->types[id];
    gen_test_prologue(g, s, id, "round_trip");
    gen_emit(s, "    struct smith_array_const_char_n text = protocol_test_text(&writer);\n");
    gen_emit(s, "    const SmithJsonValue *tree = protocol_test_parse(&arena, text);\n");
    gen_emit(s, "    %s back;\n    SmithProtocolError error = {};\n", t->name);
    gen_emit(s, "    protocol_test_accepted(%s(&arena, tree, &back, &error), &error);\n", gen_fn(g, id, "parse"));
    if (t->ambiguous) {
        gen_emit(s, "    // The schema does not tell some alternatives of a union in it apart: the document may be "
                    "read as another\n");
        gen_emit(s, "    // than the one written. What is read once is the value, and it must come back as itself.\n");
        gen_emit(s, "    value = back;\n    writer = protocol_test_writer(&arena);\n    %s(&writer, &value);\n",
                 gen_fn(g, id, "write"));
        gen_emit(s, "    text = protocol_test_text(&writer);\n    tree = protocol_test_parse(&arena, text);\n");
        gen_emit(s, "    protocol_test_accepted(%s(&arena, tree, &back, &error), &error);\n", gen_fn(g, id, "parse"));
    }
    gen_emit(s, "    protocol_test_equal(%s(&value, &back), text);\n", gen_fn(g, id, "equal"));
    gen_emit(s, "    SmithJsonWriter again = protocol_test_writer(&arena);\n    %s(&again, &back);\n",
             gen_fn(g, id, "write"));
    gen_emit(s, "    protocol_test_same_text(text, protocol_test_text(&again));\n}\n\n");
}

static void gen_test_missing(Generator *g, Sink *s, int id) {
    const GenType *t = &g->types[id];
    gen_test_prologue(g, s, id, "missing");
    gen_emit(s, "    const SmithJsonValue *tree = protocol_test_parse(&arena, protocol_test_text(&writer));\n");
    gen_emit(s, "    static const char *const required[] = {");
    bool first = true;
    for (size_t i = 0; i < t->field_count; i++) { // bounded by the members
        if (!t->fields[i].required) continue;
        gen_emit(s, "%s\"%s\"", first ? "" : ", ", t->fields[i].json);
        first = false;
    }
    gen_emit(s,
             "};\n    for (size_t i = 0; i < sizeof required / sizeof required[0]; i++) { // bounded by the table\n");
    gen_emit(s, "        const SmithJsonValue *cut = protocol_test_without(&arena, tree, required[i]);\n");
    gen_emit(s, "        %s back;\n        SmithProtocolError error = {};\n", t->name);
    gen_emit(s, "        protocol_test_refused(%s(&arena, cut, &back, &error), &error, required[i]);\n    }\n}\n\n",
             gen_fn(g, id, "parse"));
}

// ---------------------------------------------------------------------------------------------------------------
// The files

static void gen_banner(Sink *s, const char *file, const char *source, const char *license) {
    gen_emit(s, "// %s: generated by scripts/protocol.c from %s.\n", file, source);
    gen_emit(s, "// Do not edit it: `smith build protocol` writes it again, and `smith check` fails when this file\n");
    gen_emit(s, "// is not what the generator writes now.\n//\n");
    gen_emit(s, "// The schema is not smith's. Its license, whose whole text is kept beside this file (the first "
                "lines):\n//\n");
    size_t lines = 0;
    for (const char *c = license; *c != '\0' && lines < 24; lines++) { // bounded by 24 lines
        const char *end = strchr(c, '\n');
        size_t length = end != nullptr ? (size_t)(end - c) : strlen(c);
        gen_emit(s, "//%s%.*s\n", length > 0 ? "   " : "", (int)length, c);
        c += length + (end != nullptr ? 1 : 0);
    }
    gen_emit(s, "\n");
}

static void gen_header(Generator *g, Sink *s, const char *source, const char *license) {
    gen_banner(s, gen_format(g, "%s.h", g->schema), source, license);
    gen_emit(s, "#pragma once\n\n#include \"protocol.h\"\n\n");
    for (size_t k = 0; k < g->order_count; k++)
        gen_declare(g, s, g->order[k]);           // bounded by the types
    for (size_t k = 0; k < g->order_count; k++) { // bounded by the types
        for (size_t v = 0; v < 4; v++)
            gen_head(g, s, g->order[k], gen_verbs[v], false);
        gen_emit(s, "\n");
    }
}

static void gen_implementation(Generator *g, Sink *s, const char *source, const char *license) {
    gen_banner(s, gen_format(g, "%s.c", g->schema), source, license);
    gen_emit(s, "#include \"%s.h\"\n\n", g->schema);
    for (size_t k = 0; k < g->order_count; k++)
        gen_source(g, s, g->order[k]); // bounded by the types
}

static void gen_tests(Generator *g, Sink *s, const char *source, const char *license) {
    gen_banner(s, gen_format(g, "test_protocol_%s.c", g->schema), source, license);
    gen_emit(s, "#include \"%s.h\"\n#include \"protocol_support.h\"\n\n#include <smith/test.h>\n\n", g->schema);
    for (size_t k = 0; k < g->order_count; k++) { // bounded by the types
        int id = g->order[k];
        gen_test_round_trip(g, s, id);
        if (gen_required_count(&g->types[id]) > 0) gen_test_missing(g, s, id);
    }
}

static Sink gen_sink_make(char *storage) {
    storage[0] = '\0';
    return (Sink){storage, 0};
}

static struct smith_array_const_char_n gen_read_file(const char *path, char *buffer, size_t capacity) {
    FILE *file = fopen(path, "rb");
    if (file == nullptr) return (struct smith_array_const_char_n){};
    size_t length = fread(buffer, 1, capacity - 1, file);
    bool whole = feof(file) != 0;
    fclose(file);
    if (!whole) gen_die("%s is more than %zu bytes", path, capacity - 1);
    buffer[length] = '\0';
    return (struct smith_array_const_char_n){.data = buffer, .count = {length}};
}

static char gen_existing[gen_sink_capacity];

// Writes the file if its bytes changed (so a build sees no change when there is none); with `check`, only compares.
static bool gen_output(const char *directory, const char *name, const Sink *sink, bool check) {
    char path[1024];
    int written = snprintf(path, sizeof path, "%s/%s", directory, name);
    GEN_ASSERT(written > 0 && (size_t)written < sizeof path);
    struct smith_array_const_char_n old = gen_read_file(path, gen_existing, sizeof gen_existing);
    if (old.data != nullptr && old.count[0] == sink->length && memcmp(old.data, sink->begin, sink->length) == 0)
        return true;
    if (check) {
        fprintf(stderr, "protocol: %s is not what the generator writes now; run `smith build protocol`\n", path);
        return false;
    }
    FILE *file = fopen(path, "wb");
    if (file == nullptr) gen_die("cannot write %s", path);
    GEN_ASSERT(fwrite(sink->begin, 1, sink->length, file) == sink->length);
    GEN_ASSERT(fclose(file) == 0);
    return true;
}

static char gen_header_storage[gen_sink_capacity];
static char gen_source_storage[gen_sink_capacity];
static char gen_test_storage[gen_sink_capacity];
static char gen_license_storage[1 << 20];
static char gen_schema_storage[gen_input_capacity];

static void gen_prepare(Generator *g, const char *name, struct smith_array_const_char_n schema) {
    g->arena = smith_arena_make(gen_memory, sizeof gen_memory);
    g->schema = name;
    char upper[32];
    GEN_ASSERT(strlen(name) < sizeof upper && name[0] != '\0');
    memcpy(upper, name, strlen(name) + 1);
    upper[0] = (char)toupper((unsigned char)upper[0]);
    g->prefix = gen_format(g, "Smith%s", upper);
    g->stem_prefix = gen_format(g, "smith_%s_", name);
    const SmithJsonValue *root = nullptr;
    SmithJsonError error = {};
    SmithJsonLimits limits = {.depth = 64, .string_length = 1 << 20, .count = 1 << 21};
    if (!smith_json_parse(&g->arena, schema, limits, &root, &error))
        gen_die("the schema is not JSON: byte %zu: %s", error.offset, smith_json_reason_text(error.reason));
    const SmithJsonValue *definitions = gen_get(root, "definitions");
    if (definitions == nullptr) definitions = gen_get(root, "$defs");
    if (definitions == nullptr) gen_die("the schema has no definitions or $defs");
    for (int p = 0; p < gen_primitives; p++) { // table entries 0 to 5 are the primitives, so slots start at 6
        g->types[p] = (GenType){.kind = (GenKind)p, .element = -1, .visit = 2};
        g->redirect[p] = p;
    }
    g->type_count = gen_primitives;
    gen_read(g, definitions);
    gen_settle(g);
    gen_name_wrappers(g);
    for (size_t k = 0; k < g->order_count; k++) { // bounded by the types
        if (g->types[g->order[k]].kind == GenKind_Union) gen_finish_union(g, g->order[k]);
    }
    gen_mark_ambiguous(g);
}

static size_t gen_lines(const Sink *sink) {
    size_t lines = 0;
    for (size_t i = 0; i < sink->length; i++) { // bounded by the text
        if (sink->begin[i] == '\n') lines++;
    }
    return lines;
}

int main(int argc, char **argv) {
    bool check = argc == 8 && strcmp(argv[7], "--check") == 0;
    if (argc != 7 && !check) gen_die("usage: protocol NAME SCHEMA LICENSE SOURCE OUT_DIR TEST_DIR [--check]");
    Generator *g = &generator;
    struct smith_array_const_char_n schema = gen_read_file(argv[2], gen_schema_storage, sizeof gen_schema_storage);
    if (schema.data == nullptr) gen_die("cannot read %s", argv[2]);
    struct smith_array_const_char_n license = gen_read_file(argv[3], gen_license_storage, sizeof gen_license_storage);
    if (license.data == nullptr) gen_die("cannot read %s", argv[3]);
    gen_prepare(g, argv[1], schema);
    Sink header = gen_sink_make(gen_header_storage);
    Sink source = gen_sink_make(gen_source_storage);
    Sink tests = gen_sink_make(gen_test_storage);
    gen_header(g, &header, argv[4], license.data);
    gen_implementation(g, &source, argv[4], license.data);
    gen_tests(g, &tests, argv[4], license.data);
    Sink copy = {gen_license_storage, license.count[0]};
    bool same = gen_output(argv[5], gen_format(g, "%s.h", argv[1]), &header, check);
    same = gen_output(argv[5], gen_format(g, "%s.c", argv[1]), &source, check) && same;
    same = gen_output(argv[5], gen_format(g, "LICENSE-%s.txt", argv[1]), &copy, check) && same;
    same = gen_output(argv[6], gen_format(g, "test_protocol_%s.c", argv[1]), &tests, check) && same;
    if (!check) {
        size_t ambiguous = 0;
        for (size_t k = 0; k < g->order_count; k++)
            ambiguous += g->types[g->order[k]].ambiguous ? 1 : 0; // bounded
        fprintf(stderr,
                "protocol: %s: %zu types (%zu hold a union that does not tell its alternatives apart); %zu lines of "
                "header, %zu of source, %zu of tests\n",
                argv[1], g->order_count, ambiguous, gen_lines(&header), gen_lines(&source), gen_lines(&tests));
    }
    return same ? 0 : 1;
}
