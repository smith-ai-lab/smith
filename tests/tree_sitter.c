// Parses the files named on the command line with the .smith grammar (the shared library whose path is in
// $SMITH_TREE_SITTER, else the one `smith cc --smith-tree-sitter` prints) and the pinned tree-sitter runtime, and fails
// on any ERROR or MISSING node. Usage: tree_sitter [--expect-error | --corpus] <file>... With --expect-error each file
// must have such a node (the cases whose text is not valid .smith on purpose). With --corpus each file is a tree-sitter
// corpus file (tree-sitter/test/corpus): every test's input must parse to the tree written under its dashes, compared
// without whitespace and without field names (the format the tree-sitter CLI writes), so the tests run without the CLI
// or Node. Prints "ok <file>" or "FAIL <file>:<row>:<column> <what>" per file; the exit status is the number of
// failures (at most 125). Nothing is allocated by this program; the runtime allocates its own trees.
#include <ctype.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tree_sitter/api.h>

constexpr size_t max_file = 8UL * 1024 * 1024;
constexpr size_t max_steps = 64UL * 1024 * 1024;

static char source[max_file + 1];

// The first ERROR or MISSING node in tree order, found with a cursor (no recursion); false when there is none.
static bool find_problem(TSNode root, TSNode *found) {
    TSTreeCursor cursor = ts_tree_cursor_new(root);
    bool result = false;
    for (size_t step = 0; step < max_steps; step++) {
        TSNode node = ts_tree_cursor_current_node(&cursor);
        if (ts_node_is_error(node) || ts_node_is_missing(node)) {
            *found = node;
            result = true;
            break;
        }
        // Subtrees without a problem are skipped whole.
        if (ts_node_has_error(node) && ts_tree_cursor_goto_first_child(&cursor)) continue;
        while (!ts_tree_cursor_goto_next_sibling(&cursor)) {
            if (!ts_tree_cursor_goto_parent(&cursor)) goto done;
        }
    }
done:
    ts_tree_cursor_delete(&cursor);
    return result;
}

static bool read_file(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    if (file == nullptr) return false;
    *size = fread(source, 1, max_file, file);
    bool ok = feof(file) != 0 && ferror(file) == 0;
    fclose(file);
    return ok;
}

constexpr size_t max_text = 256UL * 1024;

typedef enum { CorpusState_Header, CorpusState_Name, CorpusState_Input, CorpusState_Expected } CorpusState;

typedef struct {
    char *data;
    size_t used;
} Text;

static void text_add(Text *text, const char *piece, size_t length) {
    if (text->used + length >= max_text) return; // a tree too big to compare is cut, and then differs
    memcpy(text->data + text->used, piece, length);
    text->used += length;
    text->data[text->used] = '\0';
}

static void text_node(Text *text, TSNode node, bool opening) {
    if (!ts_node_is_named(node)) return;
    if (opening) {
        text_add(text, "(", 1);
        text_add(text, ts_node_type(node), strlen(ts_node_type(node)));
    } else {
        text_add(text, ")", 1);
    }
}

// The tree as the CLI writes it, named nodes only, without spaces: (a(b)(c)). A cursor walk, no recursion.
static void tree_text(TSNode root, Text *text) {
    text->used = 0;
    text->data[0] = '\0';
    TSTreeCursor cursor = ts_tree_cursor_new(root);
    text_node(text, root, true);
    bool finished = false;
    for (size_t step = 0; step < max_steps && !finished; step++) {
        if (ts_tree_cursor_goto_first_child(&cursor)) {
            text_node(text, ts_tree_cursor_current_node(&cursor), true);
            continue;
        }
        for (size_t up = 0; up < max_steps; up++) {
            text_node(text, ts_tree_cursor_current_node(&cursor), false);
            if (ts_tree_cursor_goto_next_sibling(&cursor)) {
                text_node(text, ts_tree_cursor_current_node(&cursor), true);
                break;
            }
            if (!ts_tree_cursor_goto_parent(&cursor)) {
                finished = true;
                break;
            }
        }
    }
    ts_tree_cursor_delete(&cursor);
}

static void strip_spaces(char *text) {
    size_t to = 0;
    for (size_t from = 0; text[from] != '\0'; from++) {
        if (isspace((unsigned char)text[from]) == 0) text[to++] = text[from];
    }
    text[to] = '\0';
}

static bool all_of(const char *line, char c, size_t at_least) {
    size_t n = 0;
    while (line[n] == c)
        n++;
    return n >= at_least && (line[n] == '\n' || line[n] == '\0');
}

static char corpus_input[max_text];
static char corpus_expected[max_text];
static char corpus_actual[max_text];

// Runs the tests of one corpus file; returns the number that failed.
static int run_corpus(TSParser *parser, const char *path, size_t size, int *count) {
    int failures = 0;
    char *cursor = source;
    char *end = source + size;
    source[size] = '\0';
    char name[512] = "";
    CorpusState state = CorpusState_Header;
    size_t input_used = 0;
    size_t expected_used = 0;
    Text actual = {.data = corpus_actual};
    for (size_t guard = 0; guard < max_steps && cursor < end + 1; guard++) {
        char *newline = strchr(cursor, '\n');
        size_t length = newline != nullptr ? (size_t)(newline - cursor) + 1 : (size_t)(end - cursor);
        char line[1024];
        snprintf(line, sizeof line, "%.*s", (int)(length < sizeof line - 1 ? length : sizeof line - 1), cursor);
        bool at_end = cursor >= end;
        bool header = !at_end && all_of(line, '=', 3);
        if (at_end || (header && state == CorpusState_Expected)) {
            if (state == CorpusState_Expected) {
                corpus_input[input_used] = '\0';
                corpus_expected[expected_used] = '\0';
                TSTree *tree = ts_parser_parse_string(parser, nullptr, corpus_input, (uint32_t)input_used);
                tree_text(ts_tree_root_node(tree), &actual);
                ts_tree_delete(tree);
                strip_spaces(corpus_expected);
                (*count)++;
                if (strcmp(corpus_expected, corpus_actual) == 0) {
                    printf("ok    %s: %s\n", path, name);
                } else {
                    printf("FAIL  %s: %s\n  expected: %s\n  actual:   %s\n", path, name, corpus_expected,
                           corpus_actual);
                    failures++;
                }
            }
            state = header ? CorpusState_Name : CorpusState_Header;
            if (at_end) break;
        } else if (state == CorpusState_Header && header) {
            state = CorpusState_Name;
        } else if (state == CorpusState_Name && header) {
            state = CorpusState_Input;
            input_used = 0;
            expected_used = 0;
        } else if (state == CorpusState_Name) {
            snprintf(name, sizeof name, "%.*s", (int)strcspn(line, "\n"), line);
        } else if (state == CorpusState_Input && all_of(line, '-', 3)) {
            state = CorpusState_Expected;
        } else if (state == CorpusState_Input && input_used + length < max_text) {
            memcpy(corpus_input + input_used, cursor, length);
            input_used += length;
        } else if (state == CorpusState_Expected && expected_used + length < max_text) {
            memcpy(corpus_expected + expected_used, cursor, length);
            expected_used += length;
        }
        cursor += length;
    }
    return failures;
}

int main(int argc, char **argv) {
    bool expect_error = argc >= 2 && strcmp(argv[1], "--expect-error") == 0;
    bool corpus = argc >= 2 && strcmp(argv[1], "--corpus") == 0;
    int first = expect_error || corpus ? 2 : 1;
    char printed[4096] = "";
    const char *library = getenv("SMITH_TREE_SITTER");
    if (library == nullptr) { // else asks the smith on the PATH, as the Neovim plugin does
        FILE *query = popen("smith cc --smith-tree-sitter", "r");
        if (query != nullptr && fgets(printed, sizeof printed, query) != nullptr) library = printed;
        if (query != nullptr) pclose(query);
        printed[strcspn(printed, "\n")] = '\0';
    }
    void *handle = library != nullptr ? dlopen(library, RTLD_NOW) : nullptr;
    const TSLanguage *(*language)(void) =
        handle != nullptr ? (const TSLanguage *(*)(void))dlsym(handle, "tree_sitter_smith") : nullptr;
    if (language == nullptr) {
        fprintf(stderr, "tree_sitter: cannot load tree_sitter_smith from $SMITH_TREE_SITTER (%s)\n", library);
        return 127;
    }
    TSParser *parser = ts_parser_new();
    if (!ts_parser_set_language(parser, language())) {
        fprintf(stderr, "tree_sitter: the parser's ABI is not the runtime's\n");
        return 126;
    }
    int failures = 0;
    int tests = 0;
    for (int i = first; i < argc; i++) { // bounded by argc
        size_t size = 0;
        if (!read_file(argv[i], &size)) {
            printf("FAIL %s: cannot be read\n", argv[i]);
            failures++;
            continue;
        }
        if (corpus) {
            failures += run_corpus(parser, argv[i], size, &tests);
            continue;
        }
        TSTree *tree = ts_parser_parse_string(parser, nullptr, source, (uint32_t)size);
        TSNode problem;
        bool bad = find_problem(ts_tree_root_node(tree), &problem);
        if (bad == expect_error) {
            printf("ok    %s\n", argv[i]);
        } else if (bad) {
            TSPoint at = ts_node_start_point(problem);
            printf("FAIL  %s:%u:%u %s %s\n", argv[i], at.row + 1, at.column + 1,
                   ts_node_is_missing(problem) ? "MISSING" : "ERROR", ts_node_type(problem));
            failures++;
        } else {
            printf("FAIL  %s: parsed without an ERROR or MISSING node, but one is expected\n", argv[i]);
            failures++;
        }
        ts_tree_delete(tree);
    }
    ts_parser_delete(parser);
    if (corpus) printf("%d corpus tests, %d failed\n", tests, failures);
    return failures < 125 ? failures : 125;
}
