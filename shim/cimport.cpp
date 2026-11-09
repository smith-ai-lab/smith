// The edge to clang for `import <h>;` in the new front end (#1112 track C, #1114): one header in, its functions and
// structs out as flat data, read through clang's C++ libraries (clangTooling, clangFrontend, clangAST), which smith
// already links. No libclang (C API), no codegen and no modules: the header is parsed to an AST and walked.
//
// The result is one array of 32-bit words, with no pointers, that front::cimport (front/cimport.smith) reads. The
// layout is `smith_cimport_*` below; keep it in step with front/cimport.smith.
//
//   words[0]      version (1)
//   words[1]      total words
//   words[2..3]   string pool: byte offset from the start of the blob, byte size (strings are NUL-terminated; offset 0
//                 is the empty string)
//   words[4..5]   types: first word, count      (5 words each: kind, flags, a, b, c)
//   words[6..7]   functions: first word, count  (5 words each: name, return type, first parameter, parameter count,
//                 flags)
//   words[8..9]   structs: first word, count    (4 words each: name, first field, field count, flags: bit 0 opaque,
//                 bit 1 union)
//   words[10..11] fields: first word, count     (2 words each: name, type)
//   words[12..13] parameters: first word, count (2 words each: name, type)
//   words[14..15] not imported: first word, count (3 words each: kind, name, reason)
//   words[16]     error text (a string offset), 0 when the header was read
//   words[17..18] enums: first word, count      (4 words each: name, underlying type, first item, item count; named
//                 enums only)
//   words[19..20] enum items: first word, count (4 words each: name, low word, high word, 1 when negative; the value is
//                 the magnitude)
//   words[21..22] constants: first word, count  (6 words each: name, type, kind, negative, low, high; kind 0 integer
//                 (the magnitude), 1 floating (the magnitude's IEEE double), 2 string (low = byte offset in the pool,
//                 high = byte count), 3 an extern variable (no value). The enumerators of an anonymous enum, the
//                 object-like macros whose value clang folds to a constant, the header's `static const` variables with
//                 a constant initializer, and its extern variables (`stdout`).)
//   words[23..24] places: first word, count     (6 words each: kind, ordinal, file, line, column, doc; where a
//                 declaration is and the comment above it (#1941). The file, line and column are clang's presumed
//                 location (the header as written, after `#line`); file and doc are string offsets, doc is 0 when
//                 there is no comment, and its text has no `//`, `///` or `/** */` markers. A declaration with no
//                 location (a builtin) has no row. kind 0 function (ordinal = its index in functions), 1 struct, 2
//                 field (index in fields), 3 enum, 4 enum item (index in enum items), 5 constant (index in constants:
//                 an anonymous enum's enumerator, a macro, a `static const` variable), 6 typedef (ordinal = the
//                 string offset of its name).)
//
// Type kinds: 0 void; 1 bool; 2 integer (a = bits, b = 1 when signed, 2 for plain `char`); 3 floating (a = bits); 4
// pointer (a = pointee); 5 array (a = element, b = count); 6 typedef (a = name, b = underlying); 7 struct (a = struct
// index); 8 function (a = return type, b = first parameter, c = parameter count; flags bit 2 = variadic); 9 unsupported
// (a = the spelling of the C type that has no smith equivalent: `long double`, a bit-field); 11 string
// (smith's `Str`, the type of a string macro); 10 function pointer (a = the function type; smith has no
// pointer-to-function syntax, it is `Fn` applied to the function type). Type flags: 1 const, 2 restrict. A declaration
// that uses an unsupported type is still read, with that type, and is also in the not imported list with the reason:
// the use is what reports it. Struct flags: 1 opaque (no definition). Not imported kinds: 0 declaration, 1 attribute
// (name is the declaration's, reason the attribute's spelling).
//
// Macros (#1127): a first run only preprocesses the header, to list its object-like macros. The one parse then reads
// the header and, after it, one line `static const __typeof__((M)) __smith_m_M = (M);` for each candidate, so clang
// folds each value; an error on such a line (line 2 and after of the source) is that macro's reason for not being a
// constant. The rule for the preprocessor's own plumbing, left out silently: a name that starts with `__`, an empty
// macro (include guards among them) and a macro whose tokens are all keywords or punctuation (`#define restrict
// __restrict`). A function-like macro is listed as such.
//
// Memory: the caller's buffer. When it is too small the call returns the words needed and the caller calls again.
#include <algorithm>
#include <array>
#include <climits>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unistd.h>
#include <vector>

#include "clang/AST/APValue.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RawCommentList.h"
#include "clang/AST/Type.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/raw_ostream.h"

extern "C" {
// Keep in step with front/cimport.smith.
typedef struct {
    const char *name;   // as written in the import
    int32_t quoted;     // 1 for "h", 0 for <h>
    const char *folder; // the importer's folder, for "h"
    const char *flags;  // -I, -D, -isysroot, ... one for each line
} SmithCimportRequest;
}

// lib/handlers.c, linked into smith and into every runner: `new` and `malloc` reach the allocator in force. nullptr is
// plain malloc.
struct SmithAllocator;
extern "C" SmithAllocator *smith_handlers_set_allocator(SmithAllocator *allocator);

namespace {

// Clang and LLVM keep state for the life of the process (the real file system, registries) that the first call
// allocates. A test or a caller puts an arena in force, and that state would then die with the arena, so the read runs
// on plain malloc, and puts back the allocator it found. What the caller gets is only the words written into its
// buffer.
struct PlainAllocator {
    SmithAllocator *before = smith_handlers_set_allocator(nullptr);
    ~PlainAllocator() {
        smith_handlers_set_allocator(before);
    }
};

enum Kind : uint32_t {
    Void,
    Bool,
    Integer,
    Floating,
    Pointer,
    Array,
    Typedef,
    Struct,
    Function,
    Unsupported,
    FnPointer,
    String
};

// An object-like or function-like macro of the header, in source order.
struct Macro {
    std::string name;
    bool function_like;
    std::string file; // where it is defined, and the comment above it
    unsigned line = 0, column = 0;
    std::string doc;
};

constexpr const char *source_name = "smith_cimport.c";

struct Reader {
    clang::ASTContext *context_ptr;
    clang::ASTContext &context() {
        return *context_ptr;
    }
    std::string pool = std::string(1, '\0'); // offset 0 is the empty string
    std::map<std::string, uint32_t> strings;
    std::vector<std::array<uint32_t, 5>> types;
    std::map<std::array<uint32_t, 5>, uint32_t> type_index;
    std::vector<std::array<uint32_t, 5>> functions;
    std::vector<std::array<uint32_t, 4>> structs;
    std::map<const clang::RecordDecl *, uint32_t> struct_index;
    std::vector<std::array<uint32_t, 2>> fields, parameters;
    std::vector<std::array<uint32_t, 3>> not_imported;
    std::vector<std::array<uint32_t, 4>> enums, enum_items;
    std::vector<std::array<uint32_t, 6>> constants;
    std::vector<std::array<uint32_t, 6>> places;
    std::set<uint32_t> typedef_placed;
    std::string failure; // why the first type of the declaration in hand could not be imported
    void fail(const std::string &why) {
        if (failure.empty()) failure = why;
    }

    explicit Reader(clang::ASTContext *c) : context_ptr(c) {}

    uint32_t text(const std::string &s) {
        if (s.empty()) return 0;
        auto found = strings.find(s);
        if (found != strings.end()) return found->second;
        uint32_t at = static_cast<uint32_t>(pool.size());
        pool += s;
        pool += '\0';
        strings[s] = at;
        return at;
    }

    uint32_t type_node(uint32_t kind, uint32_t flags, uint32_t a, uint32_t b, uint32_t c) {
        std::array<uint32_t, 5> node{kind, flags, a, b, c};
        auto found = type_index.find(node);
        if (found != type_index.end()) return found->second;
        uint32_t at = static_cast<uint32_t>(types.size());
        types.push_back(node);
        type_index[node] = at;
        return at;
    }

    // The place and comment of `decl`, as a row of `places`; nothing for a declaration with no location.
    void place(uint32_t kind, uint32_t ordinal, const clang::Decl *decl) {
        const clang::SourceManager &sm = context().getSourceManager();
        clang::PresumedLoc loc = sm.getPresumedLoc(decl->getLocation());
        if (!loc.isValid() || loc.getFilename()[0] == '<' || std::strcmp(loc.getFilename(), source_name) == 0) return;
        std::string doc;
        // Clang gives an enumerator the comment of the one before it; only a comment that ends on the line above the
        // declaration, or on its line, is its own.
        const clang::RawComment *raw = context().getRawCommentForAnyRedecl(decl);
        if (raw != nullptr &&
            sm.getPresumedLineNumber(raw->getEndLoc()) + 1 >= sm.getPresumedLineNumber(decl->getBeginLoc()))
            doc = llvm::StringRef(raw->getFormattedText(sm, context().getDiagnostics())).trim().str();
        place_at(kind, ordinal, loc.getFilename(), loc.getLine(), loc.getColumn(), doc);
    }
    void place_at(uint32_t kind, uint32_t ordinal, const std::string &file, unsigned line, unsigned column,
                  const std::string &doc) {
        places.push_back({kind, ordinal, text(file), line, column, text(doc)});
    }

    void skip(uint32_t kind, const std::string &name, const std::string &reason) {
        not_imported.push_back({kind, text(name), text(reason)});
    }

    // The struct of a record, registered (and its fields read) the first time it is met.
    uint32_t record(const clang::RecordDecl *decl);
    // The type, or UINT32_MAX with `failure` set. `behind` is true under a pointer, where a struct that could not be
    // imported stays usable as an opaque one.
    uint32_t convert(clang::QualType type, bool behind);
    clang::QualType without_volatile_pointee(clang::QualType type);
    uint32_t convert_inner(clang::QualType type, bool behind);
    uint32_t builtin(const clang::BuiltinType *type, uint32_t flags);
    uint32_t function_type(const clang::FunctionProtoType *proto, uint32_t flags);
    void function(const clang::FunctionDecl *decl);
    void walk(const clang::TranslationUnitDecl *unit, const std::vector<Macro> &macros,
              const std::map<unsigned, std::string> &errors);
    // The magnitude of an integer, as two words and a sign.
    static std::array<uint32_t, 3> split(const llvm::APInt &magnitude, bool negative);
    // A constant of `type` whose value clang folded in `init`; false, with `why`, when it cannot be one.
    bool constant(const std::string &name, clang::QualType type, const clang::Expr *init, std::string &why);
    void enumeration(const clang::EnumDecl *decl);
    void macros_of(const clang::TranslationUnitDecl *unit, const std::vector<Macro> &macros,
                   const std::map<unsigned, std::string> &errors);
};

constexpr uint32_t bad = UINT32_MAX;

bool from_header(const clang::Decl *decl, const clang::SourceManager &sm) {
    if (decl->isImplicit()) return false;
    clang::PresumedLoc loc = sm.getPresumedLoc(decl->getLocation());
    if (!loc.isValid()) return false;
    return loc.getFilename()[0] != '<' && std::strcmp(loc.getFilename(), source_name) != 0;
}

uint32_t Reader::builtin(const clang::BuiltinType *type, uint32_t flags) {
    using B = clang::BuiltinType;
    if (type->getKind() == B::Void) return type_node(Void, flags, 0, 0, 0);
    if (type->getKind() == B::Bool) return type_node(Bool, flags, 0, 0, 0);
    if (type->isInteger()) {
        uint32_t bits = static_cast<uint32_t>(context().getTypeSize(clang::QualType(type, 0)));
        bool plain_char = type->getKind() == B::Char_S || type->getKind() == B::Char_U;
        return type_node(Integer, flags, bits, plain_char ? 2 : (type->isSignedInteger() ? 1 : 0), 0);
    }
    if (type->isFloatingPoint()) {
        uint32_t bits = static_cast<uint32_t>(context().getTypeSize(clang::QualType(type, 0)));
        if (type->getKind() == B::Float || type->getKind() == B::Double || type->getKind() == B::Half)
            return type_node(Floating, flags, bits, 0, 0);
    }
    fail(std::string("the type `") + type->getName(context().getPrintingPolicy()).str() + "` is not imported");
    return bad;
}

uint32_t Reader::function_type(const clang::FunctionProtoType *proto, uint32_t flags) {
    uint32_t result = convert(proto->getReturnType(), false);
    if (result == bad) return bad;
    std::vector<uint32_t> kinds;
    for (clang::QualType p : proto->param_types()) {
        p = without_volatile_pointee(p);
        uint32_t t = convert(p, false);
        if (t == bad) return bad;
        kinds.push_back(t);
    }
    // After the loop: a parameter that is itself a function pointer adds its own parameters to the pool (#2643).
    uint32_t first = static_cast<uint32_t>(parameters.size());
    for (uint32_t t : kinds)
        parameters.push_back({0, t});
    return type_node(Function, flags | (proto->isVariadic() ? 4U : 0U), result, first,
                     static_cast<uint32_t>(kinds.size()));
}

// A parameter `const volatile void *` (the sanitizer's `__asan_poison_memory_region`) takes any pointer, so a call
// loses nothing when the pointee is read as not volatile. Only the pointee of a parameter is relaxed: a volatile
// variable, field or result stays not imported.
clang::QualType Reader::without_volatile_pointee(clang::QualType type) {
    const auto *pointer = type->getAs<clang::PointerType>();
    if (pointer == nullptr || !pointer->getPointeeType().isVolatileQualified()) return type;
    clang::QualType pointee = pointer->getPointeeType();
    pointee.removeLocalVolatile();
    return context().getQualifiedType(context().getPointerType(pointee), type.getQualifiers());
}

uint32_t Reader::convert(clang::QualType type, bool behind) {
    uint32_t t = convert_inner(type, behind);
    return t != bad ? t : type_node(Unsupported, 0, text(type.getAsString()), 0, 0);
}

uint32_t Reader::convert_inner(clang::QualType type, bool behind) {
    uint32_t flags = 0;
    if (type.isConstQualified()) flags |= 1;
    if (type.isRestrictQualified()) flags |= 2;
    if (type.isVolatileQualified()) {
        fail("a volatile type is not imported");
        return bad;
    }
    if (const auto *t = type->getAs<clang::TypedefType>()) {
        const clang::TypedefNameDecl *name = t->getDecl();
        uint32_t under = convert(name->getUnderlyingType(), behind);
        if (under == bad) return bad;
        uint32_t spelled = text(name->getName().str());
        if (typedef_placed.insert(spelled).second) place(6, spelled, name);
        return type_node(Typedef, flags, spelled, under, 0);
    }
    type = type.getUnqualifiedType();
    if (const auto *t = type->getAs<clang::BuiltinType>()) return builtin(t, flags);
    if (const auto *t = type->getAs<clang::PointerType>()) {
        uint32_t pointee = convert(t->getPointeeType(), true);
        if (pointee == bad) return bad;
        bool to_function = types[pointee][0] == Function;
        return type_node(to_function ? FnPointer : Pointer, flags, pointee, 0, 0);
    }
    if (const auto *t = llvm::dyn_cast<clang::ConstantArrayType>(type.getTypePtr())) {
        uint32_t element = convert(t->getElementType(), behind);
        if (element == bad) return bad;
        return type_node(Array, flags, element, static_cast<uint32_t>(t->getSize().getZExtValue()), 0);
    }
    if (type->isArrayType()) {
        fail("an array without a fixed size is not imported");
        return bad;
    }
    if (const auto *t = type->getAs<clang::FunctionProtoType>()) return function_type(t, flags);
    if (type->isFunctionNoProtoType()) {
        fail("a function without a prototype is not imported");
        return bad;
    }
    if (const auto *t = type->getAs<clang::RecordType>()) {
        const clang::RecordDecl *decl = t->getDecl();
        uint32_t index = record(decl);
        return type_node(Struct, flags, index, 0, 0);
    }
    if (type->isEnumeralType()) { // C2 reads enums; until then they are their integer type
        return convert(type->getAs<clang::EnumType>()->getDecl()->getIntegerType(), behind);
    }
    fail(std::string("the type `") + type.getAsString() + "` is not imported");
    return bad;
}

uint32_t Reader::record(const clang::RecordDecl *decl) {
    const clang::RecordDecl *definition = decl->getDefinition();
    const clang::RecordDecl *key =
        definition != nullptr ? definition : llvm::cast<clang::RecordDecl>(decl->getCanonicalDecl());
    auto found = struct_index.find(key);
    if (found != struct_index.end()) return found->second;
    uint32_t index = static_cast<uint32_t>(structs.size());
    struct_index[key] = index;
    std::string name = key->getName().str();
    if (name.empty())
        if (const clang::TypedefNameDecl *t = key->getTypedefNameForAnonDecl()) name = t->getName().str();
    // An anonymous member (`union { long ru_maxrss; };`) is a type no one names: the checker reaches its fields
    // through the record that holds it, so it gets a name of its own (#2456).
    if (name.empty()) name = "smith_anonymous_" + std::to_string(index);
    structs.push_back({text(name), 0, 0, 1});
    place(1, index, key);
    if (definition == nullptr) return index; // opaque
    std::string outer = failure;             // the declaration that met this struct keeps its own reason
    failure.clear();
    std::vector<std::array<uint32_t, 2>> read;
    std::vector<const clang::FieldDecl *> read_decls;
    for (const clang::FieldDecl *field : definition->fields()) {
        read_decls.push_back(field);
        uint32_t t;
        if (field->isBitField()) {
            fail("the field `" + field->getName().str() + "` is a bit-field");
            t = type_node(Unsupported, 0, text("bit-field " + field->getType().getAsString()), 0, 0);
        } else {
            t = convert(field->getType(), false);
        }
        read.push_back({text(field->getName().str()), t});
    }
    if (!failure.empty()) skip(0, "struct " + name, failure);
    failure = outer;
    structs[index][1] = static_cast<uint32_t>(fields.size());
    structs[index][2] = static_cast<uint32_t>(read.size());
    structs[index][3] = key->isUnion() ? 2 : 0; // bit 0 opaque, bit 1 union (#2456)
    for (size_t i = 0; i < read.size(); i++) {
        place(2, static_cast<uint32_t>(fields.size()), read_decls[i]);
        fields.push_back(read[i]);
    }
    return index;
}

void Reader::function(const clang::FunctionDecl *decl) {
    std::string name = decl->getName().str();
    if (decl->doesThisDeclarationHaveABody()) {
        skip(0, name, "an inline function with a body is not imported");
        return;
    }
    // The type as written: a redeclaration of a library function (strlen) takes the builtin's type, which has lost
    // the typedef names (size_t).
    clang::QualType written =
        decl->getTypeSourceInfo() != nullptr ? decl->getTypeSourceInfo()->getType() : decl->getType();
    const auto *proto = written->getAs<clang::FunctionProtoType>();
    if (proto == nullptr) {
        skip(0, name, "a function without a prototype is not imported");
        return;
    }
    failure.clear();
    uint32_t result = convert(proto->getReturnType(), false);
    std::vector<std::array<uint32_t, 2>> params;
    for (unsigned i = 0; i < decl->getNumParams() && i < proto->getNumParams(); i++)
        params.push_back({text(decl->getParamDecl(i)->getName().str()),
                          convert(without_volatile_pointee(proto->getParamType(i)), false)});
    if (!failure.empty()) skip(0, name, failure);
    uint32_t first = static_cast<uint32_t>(parameters.size());
    for (auto &p : params)
        parameters.push_back(p);
    functions.push_back(
        {text(name), result, first, static_cast<uint32_t>(params.size()), proto->isVariadic() ? 1U : 0U});
    place(0, static_cast<uint32_t>(functions.size() - 1), decl);
    for (const clang::Attr *a : decl->attrs())
        if (!a->isImplicit()) skip(1, name, a->getSpelling());
}

std::array<uint32_t, 3> Reader::split(const llvm::APInt &magnitude, bool negative) {
    uint64_t v = magnitude.getZExtValue();
    return {static_cast<uint32_t>(v), static_cast<uint32_t>(v >> 32), negative ? 1U : 0U};
}

bool Reader::constant(const std::string &name, clang::QualType type, const clang::Expr *init, std::string &why) {
    type = type.getUnqualifiedType();
    const clang::Expr *bare = init->IgnoreParenImpCasts();
    if (const auto *literal = llvm::dyn_cast<clang::StringLiteral>(bare)) {
        if (!literal->isOrdinary() && !literal->isUTF8()) {
            why = "a wide string is not imported";
            return false;
        }
        std::string bytes = literal->getBytes().str();
        uint32_t at = static_cast<uint32_t>(pool.size());
        pool += bytes;
        pool += '\0';
        constants.push_back({text(name), type_node(String, 0, 0, 0, 0), 2, 0, at, static_cast<uint32_t>(bytes.size())});
        return true;
    }
    clang::Expr::EvalResult result;
    if (init->isValueDependent() || !init->EvaluateAsRValue(result, context())) {
        why = "not a constant";
        return false;
    }
    failure.clear();
    if (result.Val.isInt() && (type->isIntegerType() || type->isBooleanType())) {
        uint32_t t = convert(type, false);
        if (types[t][0] == Unsupported) {
            why = failure.empty() ? "its type is not imported" : failure;
            return false;
        }
        const llvm::APSInt &value = result.Val.getInt();
        bool negative = value.isSigned() && value.isNegative();
        std::array<uint32_t, 3> parts = split(negative ? llvm::APInt(-value) : llvm::APInt(value), negative);
        constants.push_back({text(name), t, 0, parts[2], parts[0], parts[1]});
        return true;
    }
    if (result.Val.isFloat() && type->isRealFloatingType()) {
        uint32_t t = convert(type, false);
        if (types[t][0] == Unsupported) {
            why = failure.empty() ? "its type is not imported" : failure;
            return false;
        }
        llvm::APFloat value = result.Val.getFloat();
        if (!value.isFinite()) {
            why = "its value is not a finite number";
            return false;
        }
        bool negative = value.isNegative();
        if (negative) value.changeSign();
        bool lost = false;
        value.convert(llvm::APFloat::IEEEdouble(), llvm::APFloat::rmNearestTiesToEven, &lost);
        uint64_t bits = value.bitcastToAPInt().getZExtValue();
        constants.push_back(
            {text(name), t, 1, negative ? 1U : 0U, static_cast<uint32_t>(bits), static_cast<uint32_t>(bits >> 32)});
        return true;
    }
    why = "its value is not an integer, a floating-point number or a string";
    return false;
}

void Reader::enumeration(const clang::EnumDecl *decl) {
    if (!decl->isCompleteDefinition()) return;
    std::string name = decl->getName().str();
    uint32_t underlying = convert(decl->getIntegerType(), false);
    // An anonymous enum (a typedef gives it a name for the type, which is already its integer type) is a constant for
    // each enumerator; a named one is an enumeration.
    bool named = !name.empty();
    uint32_t first = static_cast<uint32_t>(enum_items.size());
    uint32_t count = 0;
    for (const clang::EnumConstantDecl *item : decl->enumerators()) {
        const llvm::APSInt &value = item->getInitVal();
        bool negative = value.isSigned() && value.isNegative();
        std::array<uint32_t, 3> parts = split(negative ? llvm::APInt(-value) : llvm::APInt(value), negative);
        if (named) {
            place(4, static_cast<uint32_t>(enum_items.size()), item);
            enum_items.push_back({text(item->getName().str()), parts[0], parts[1], parts[2]});
            count++;
        } else {
            place(5, static_cast<uint32_t>(constants.size()), item);
            constants.push_back({text(item->getName().str()), underlying, 0, parts[2], parts[0], parts[1]});
        }
    }
    if (named) {
        place(3, static_cast<uint32_t>(enums.size()), decl);
        enums.push_back({text(name), underlying, first, count});
    }
}

void Reader::macros_of(const clang::TranslationUnitDecl *unit, const std::vector<Macro> &list,
                       const std::map<unsigned, std::string> &errors) {
    const clang::SourceManager &sm = context().getSourceManager();
    std::map<std::string, const clang::VarDecl *> values;
    for (const clang::Decl *d : unit->decls()) {
        const auto *v = llvm::dyn_cast<clang::VarDecl>(d);
        clang::PresumedLoc loc = v != nullptr ? sm.getPresumedLoc(v->getLocation()) : clang::PresumedLoc();
        if (v != nullptr && loc.isValid() && std::strcmp(loc.getFilename(), source_name) == 0)
            values[v->getName().str()] = v;
    }
    unsigned line = 2;
    for (const Macro &macro : list) {
        if (macro.function_like) {
            skip(0, macro.name, "a function-like macro");
            continue;
        }
        unsigned here = line++;
        auto found = values.find("__smith_m_" + macro.name);
        std::string why;
        const clang::VarDecl *v = found != values.end() ? found->second : nullptr;
        if (v != nullptr && !v->isInvalidDecl() && v->getInit() != nullptr) {
            if (constant(macro.name, v->getType(), v->getInit(), why)) {
                if (macro.line != 0)
                    place_at(5, static_cast<uint32_t>(constants.size() - 1), macro.file, macro.line, macro.column,
                             macro.doc);
                continue;
            }
        } else {
            auto error = errors.find(here);
            why = "not a constant" + (error != errors.end() ? ": " + error->second : std::string());
        }
        skip(0, macro.name, why);
    }
}

void Reader::walk(const clang::TranslationUnitDecl *unit, const std::vector<Macro> &macros,
                  const std::map<unsigned, std::string> &errors) {
    const clang::SourceManager &sm = context().getSourceManager();
    std::map<std::string, bool> seen;
    for (const clang::Decl *d : unit->decls()) {
        if (!from_header(d, sm)) continue;
        if (const auto *f = llvm::dyn_cast<clang::FunctionDecl>(d)) {
            if (seen.count(f->getName().str())) continue;
            seen[f->getName().str()] = true;
            function(f);
        } else if (const auto *r = llvm::dyn_cast<clang::RecordDecl>(d)) {
            if (r->isThisDeclarationADefinition() || r->getDefinition() == nullptr) record(r);
        } else if (const auto *e = llvm::dyn_cast<clang::EnumDecl>(d)) {
            enumeration(e);
        } else if (const auto *t = llvm::dyn_cast<clang::TypedefNameDecl>(d)) {
            // A typedef no function mentions is still the header's (stdint.int32_t, declared by an include of it).
            uint32_t spelled = text(t->getName().str());
            uint32_t under = typedef_placed.count(spelled) ? bad : convert(t->getUnderlyingType(), false);
            if (under != bad && typedef_placed.insert(spelled).second) {
                type_node(Typedef, 0, spelled, under, 0);
                place(6, spelled, t);
            }
            failure.clear();
        } else if (const auto *v = llvm::dyn_cast<clang::VarDecl>(d)) {
            // A `static const` or `constexpr` variable with a constant initializer; other variables are not read.
            std::string why;
            if (v->getType().isConstQualified() && v->getInit() != nullptr && !v->getType()->isPointerType() &&
                constant(v->getName().str(), v->getType(), v->getInit(), why))
                place(5, static_cast<uint32_t>(constants.size() - 1), v);
            else if (v->hasExternalStorage() && v->getInit() == nullptr &&
                     v->getTLSKind() == clang::VarDecl::TLS_None && !seen.count(v->getName().str())) {
                // An extern variable (`stdout`): its type and no value, kind 3.
                seen[v->getName().str()] = true;
                uint32_t type = convert(v->getType(), false);
                if (type != bad) {
                    constants.push_back({text(v->getName().str()), type, 3, 0, 0, 0});
                    place(5, static_cast<uint32_t>(constants.size() - 1), v);
                }
                failure.clear();
            }
        }
    }
    macros_of(unit, macros, errors);
}

std::vector<std::string> lines(const char *text) {
    std::vector<std::string> out;
    std::string line;
    for (const char *p = text; p != nullptr && *p != '\0'; p++) {
        if (*p == '\n') {
            if (!line.empty()) out.push_back(line);
            line.clear();
        } else {
            line += *p;
        }
    }
    if (!line.empty()) out.push_back(line);
    return out;
}

// `-I dir`, `-Idir`, `-isystem dir`, `-iquote dir` and `-isysroot dir`, in order.
std::vector<std::string> folders_of(const std::vector<std::string> &flags) {
    std::vector<std::string> out;
    for (size_t i = 0; i < flags.size(); i++) {
        const std::string &f = flags[i];
        if ((f == "-I" || f == "-isystem" || f == "-iquote") && i + 1 < flags.size())
            out.push_back(flags[++i]);
        else if (f.rfind("-I", 0) == 0 && f.size() > 2)
            out.push_back(f.substr(2));
    }
    return out;
}

class Capture : public clang::DiagnosticConsumer {
  public:
    std::string first_error;
    std::map<unsigned, std::string> line_errors; // by line of smith_cimport.c, from line 2: a macro's candidate
    void HandleDiagnostic(clang::DiagnosticsEngine::Level level, const clang::Diagnostic &info) override {
        clang::DiagnosticConsumer::HandleDiagnostic(level, info);
        if (level < clang::DiagnosticsEngine::Error) return;
        llvm::SmallString<256> text;
        info.FormatDiagnostic(text);
        if (info.hasSourceManager() && info.getLocation().isValid()) {
            clang::PresumedLoc loc = info.getSourceManager().getPresumedLoc(info.getLocation());
            if (loc.isValid() && std::strcmp(loc.getFilename(), source_name) == 0 && loc.getLine() >= 2) {
                line_errors.emplace(loc.getLine(), text.str().str());
                return;
            }
        }
        if (first_error.empty()) first_error = text.str().str();
    }
};

// The comment right above the line of `at`: the `//` or `///` lines directly before it, or one `/* */` block that
// ends on the line before, with the markers taken off. Empty when there is none.
std::string comment_above(const clang::SourceManager &sm, clang::SourceLocation at) {
    auto [id, offset] = sm.getDecomposedLoc(sm.getFileLoc(at));
    llvm::StringRef text = sm.getBufferData(id);
    std::vector<std::string> above;
    size_t end = text.rfind('\n', offset); // the end of the previous line
    bool block = false;
    while (end != llvm::StringRef::npos && end > 0) {
        size_t start = text.rfind('\n', end - 1);
        start = start == llvm::StringRef::npos ? 0 : start + 1;
        llvm::StringRef line = text.slice(start, end).trim();
        if (block) {
            above.push_back(line.str());
            if (line.contains("/*")) break;
        } else if (line.starts_with("//")) {
            above.push_back(line.ltrim('/').ltrim().str());
        } else if (line.ends_with("*/") && above.empty()) {
            block = true;
            above.push_back(line.str());
            if (line.contains("/*")) break;
        } else {
            break;
        }
        if (start == 0) break;
        end = start - 1;
    }
    std::string doc;
    for (size_t i = above.size(); i-- > 0;) {
        std::string line = above[i];
        if (block) {
            for (const char *marker : {"/**", "/*", "*/"}) {
                size_t at_marker = line.find(marker);
                if (at_marker != std::string::npos) line.erase(at_marker, std::strlen(marker));
            }
            size_t first = line.find_first_not_of(" \t*");
            line = first == std::string::npos ? "" : line.substr(first);
        }
        if (!doc.empty()) doc += '\n';
        doc += line;
    }
    return doc;
}

// A preprocess-only run that lists the header's macros: no parse, so it costs a fraction of one.
class ScanMacros : public clang::PreprocessOnlyAction {
  public:
    explicit ScanMacros(std::vector<Macro> *out) : out_(out) {}
    void ExecuteAction() override {
        clang::PreprocessOnlyAction::ExecuteAction();
        clang::Preprocessor &pp = getCompilerInstance().getPreprocessor();
        const clang::SourceManager &sm = pp.getSourceManager();
        std::vector<std::pair<clang::SourceLocation, Macro>> found;
        for (const auto &entry : pp.macros()) {
            const clang::MacroInfo *info = pp.getMacroInfo(entry.first);
            if (info == nullptr || info->isBuiltinMacro() || info->tokens_empty()) continue;
            std::string name = entry.first->getName().str();
            if (name.rfind("__", 0) == 0) continue; // implementation plumbing
            clang::PresumedLoc loc = sm.getPresumedLoc(info->getDefinitionLoc());
            if (!loc.isValid() || loc.getFilename()[0] == '<' || std::strcmp(loc.getFilename(), source_name) == 0)
                continue;
            bool value = info->isFunctionLike();
            for (const clang::Token &t : info->tokens())
                if (t.isLiteral() || t.is(clang::tok::identifier)) value = true;
            if (value) {
                Macro macro{name, info->isFunctionLike(), loc.getFilename(), loc.getLine(), loc.getColumn(), ""};
                macro.doc = comment_above(sm, info->getDefinitionLoc());
                found.push_back({info->getDefinitionLoc(), macro});
            }
        }
        std::sort(found.begin(), found.end(),
                  [&](const auto &a, const auto &b) { return sm.isBeforeInTranslationUnit(a.first, b.first); });
        for (auto &item : found)
            out_->push_back(item.second);
    }

  private:
    std::vector<Macro> *out_;
};

} // namespace

namespace {
// A result that did not fit (#2888): kept with its request, so the caller's second call, with a buffer that big, copies
// it instead of parsing the header again.
struct Kept {
    std::string request;
    std::vector<uint32_t> words;
};
Kept kept;

std::string request_key(const SmithCimportRequest *request) {
    std::string key = request->name;
    key += '\n';
    key += request->quoted != 0 ? '"' : '<';
    key += '\n';
    if (request->folder != nullptr) key += request->folder;
    key += '\n';
    if (request->flags != nullptr) key += request->flags;
    return key;
}

size_t read_words(const SmithCimportRequest *request, uint32_t *words, size_t capacity, std::vector<uint32_t> *spill,
                  std::vector<std::string> *files);
void files_read(const clang::SourceManager &sm, std::vector<std::string> *files);
bool cache_load(const SmithCimportRequest *request, std::vector<uint32_t> *words);
void cache_store(const SmithCimportRequest *request, const uint32_t *words, size_t count,
                 const std::vector<std::string> &files, int64_t started);
int64_t now_ns();

// The result for the caller: copied when it fits, else kept for the second call.
size_t deliver(const std::string &key, std::vector<uint32_t> *result, uint32_t *words, size_t capacity) {
    size_t total = result->size();
    if (total <= capacity && words != nullptr) {
        std::memcpy(words, result->data(), total * 4);
    } else {
        kept.request = key;
        kept.words = std::move(*result);
    }
    return total;
}
} // namespace

// Reads the header. Returns the words the result needs; when `capacity` is smaller nothing is written and the caller
// calls again with a buffer that big, which gets the result kept from the first call.
extern "C" size_t smith_cimport_read(const SmithCimportRequest *request, uint32_t *words, size_t capacity) {
    PlainAllocator plain;
    std::string key = request_key(request);
    if (!kept.words.empty() && kept.request == key) {
        size_t total = kept.words.size();
        if (total <= capacity && words != nullptr) {
            std::memcpy(words, kept.words.data(), total * 4);
            kept = Kept{};
        }
        return total;
    }
    std::vector<uint32_t> cached;
    if (cache_load(request, &cached)) return deliver(key, &cached, words, capacity);
    std::vector<uint32_t> spill;
    std::vector<std::string> files;
    int64_t started = now_ns();
    size_t needed = read_words(request, words, capacity, &spill, &files);
    cache_store(request, spill.empty() ? words : spill.data(), needed, files, started);
    if (!spill.empty()) {
        kept.request = key;
        kept.words = std::move(spill);
    }
    return needed;
}

namespace {
// The result goes to `words` when it fits, else to `spill`.
size_t read_words(const SmithCimportRequest *request, uint32_t *words, size_t capacity, std::vector<uint32_t> *spill,
                  std::vector<std::string> *files) {
    std::string header = request->name;
    std::vector<std::string> flags = lines(request->flags);
    std::string code = std::string("#include ") + (request->quoted != 0 ? "\"" : "<") + header +
                       (request->quoted != 0 ? "\"" : ">") + "\n";
    std::vector<std::string> args = {"-x",
                                     "c",
                                     "-std=gnu23",
                                     "-fno-color-diagnostics",
                                     "-ferror-limit=0",
                                     "-fparse-all-comments",
                                     "-fretain-comments-from-system-headers"};
    if (request->quoted != 0 && request->folder != nullptr && request->folder[0] != '\0') {
        args.push_back("-iquote");
        args.push_back(request->folder);
    }
    for (const std::string &f : flags)
        args.push_back(f);
    // The macros first (a preprocess-only run), then one parse of the header and a line for each candidate.
    std::vector<Macro> macros;
    clang::tooling::runToolOnCodeWithArgs(std::make_unique<ScanMacros>(&macros), code, args, source_name,
                                          "smith-cimport");
    for (const Macro &m : macros)
        if (!m.function_like)
            code += "static const __typeof__((" + m.name + ")) __smith_m_" + m.name + " = (" + m.name + ");\n";
    args.push_back("-fsyntax-only");
    Capture diagnostics;
    std::unique_ptr<clang::ASTUnit> unit = clang::tooling::buildASTFromCodeWithArgs(
        code, args, source_name, "smith-cimport", std::make_shared<clang::PCHContainerOperations>(),
        clang::tooling::getClangStripDependencyFileAdjuster(), clang::tooling::FileContentMappings(), &diagnostics);
    std::string error;
    if (unit == nullptr) {
        error = "cimport: could not parse `" + header + "`: " + diagnostics.first_error;
    } else if (!diagnostics.first_error.empty()) {
        error = "cimport: `" + header + "`: " + diagnostics.first_error;
        error += "; folders searched:";
        std::vector<std::string> dirs;
        if (request->quoted != 0 && request->folder != nullptr) dirs.push_back(request->folder);
        for (const std::string &d : folders_of(flags))
            dirs.push_back(d);
        clang::HeaderSearch &search = unit->getPreprocessor().getHeaderSearchInfo();
        for (auto it = search.search_dir_begin(); it != search.search_dir_end(); ++it)
            dirs.push_back(it->getName().str());
        for (const std::string &d : dirs)
            error += " " + d;
    }
    if (unit != nullptr && error.empty()) files_read(unit->getSourceManager(), files);
    Reader reader(unit != nullptr ? &unit->getASTContext() : nullptr);
    uint32_t error_at = 0;
    if (unit != nullptr && error.empty())
        reader.walk(unit->getASTContext().getTranslationUnitDecl(), macros, diagnostics.line_errors);
    if (!error.empty()) error_at = reader.text(error);

    // Lay the tables out: header, types, functions, structs, fields, parameters, not imported, then the pool.
    constexpr size_t head = 25;
    size_t at = head;
    size_t types_at = at, functions_at = types_at + reader.types.size() * 5,
           structs_at = functions_at + reader.functions.size() * 5;
    size_t fields_at = structs_at + reader.structs.size() * 4, parameters_at = fields_at + reader.fields.size() * 2;
    size_t skipped_at = parameters_at + reader.parameters.size() * 2,
           enums_at = skipped_at + reader.not_imported.size() * 3, items_at = enums_at + reader.enums.size() * 4,
           constants_at = items_at + reader.enum_items.size() * 4,
           places_at = constants_at + reader.constants.size() * 6, pool_at = places_at + reader.places.size() * 6;
    size_t pool_words = (reader.pool.size() + 3) / 4, total = pool_at + pool_words;
    if (total > capacity || words == nullptr) {
        spill->assign(total, 0);
        words = spill->data();
    }
    std::memset(words, 0, total * 4);
    uint32_t *w = words;
    w[0] = 1;
    w[1] = static_cast<uint32_t>(total);
    w[2] = static_cast<uint32_t>(pool_at * 4);
    w[3] = static_cast<uint32_t>(reader.pool.size());
    w[4] = static_cast<uint32_t>(types_at), w[5] = static_cast<uint32_t>(reader.types.size());
    w[6] = static_cast<uint32_t>(functions_at), w[7] = static_cast<uint32_t>(reader.functions.size());
    w[8] = static_cast<uint32_t>(structs_at), w[9] = static_cast<uint32_t>(reader.structs.size());
    w[10] = static_cast<uint32_t>(fields_at), w[11] = static_cast<uint32_t>(reader.fields.size());
    w[12] = static_cast<uint32_t>(parameters_at), w[13] = static_cast<uint32_t>(reader.parameters.size());
    w[14] = static_cast<uint32_t>(skipped_at), w[15] = static_cast<uint32_t>(reader.not_imported.size());
    w[16] = error_at;
    w[17] = static_cast<uint32_t>(enums_at), w[18] = static_cast<uint32_t>(reader.enums.size());
    w[19] = static_cast<uint32_t>(items_at), w[20] = static_cast<uint32_t>(reader.enum_items.size());
    w[21] = static_cast<uint32_t>(constants_at), w[22] = static_cast<uint32_t>(reader.constants.size());
    w[23] = static_cast<uint32_t>(places_at), w[24] = static_cast<uint32_t>(reader.places.size());
    auto copy = [&](size_t where, const auto &rows) {
        for (const auto &row : rows)
            for (uint32_t v : row)
                w[where++] = v;
    };
    copy(types_at, reader.types);
    copy(functions_at, reader.functions);
    copy(structs_at, reader.structs);
    copy(fields_at, reader.fields);
    copy(parameters_at, reader.parameters);
    copy(skipped_at, reader.not_imported);
    copy(enums_at, reader.enums);
    copy(items_at, reader.enum_items);
    copy(constants_at, reader.constants);
    copy(places_at, reader.places);
    std::memcpy(reinterpret_cast<char *>(w) + pool_at * 4, reader.pool.data(), reader.pool.size());
    return total;
}
} // namespace

// The disk cache of a header's words (#2891, knowledge/front-end.md "From disk"): runtime/import_cache.c keeps the
// entries, this part makes their key and their payload. The words of a header depend on the header as written, the
// importer's folder, the flags and every file clang read, so the key holds the first three and the entry lists the
// files, each with its size, time and a hash of its content: a hit hashes a file again only when its size or time
// differs (another machine, a touched file) and is a miss when any content differs or a file is gone. Two folders are
// written as tokens in the key, in the list and in the places of the words, so an entry made in another checkout or
// on another machine is the same entry: the working folder (the project) and the resource folder after
// `-resource-dir` (it sits in the cache under $HOME). A path under neither is kept as it is (/usr/include). A header
// clang could not read is never stored. The functions are weak: a program that links this shim without smith's
// runtime has no cache.
extern "C" {
[[gnu::weak]] const char *smith_import_cache_folder(void);
[[gnu::weak]] void smith_import_cache_key(const void *material, size_t size, char key[33]);
[[gnu::weak]] size_t smith_import_cache_load(const char *kind, const char *key, uint8_t *out, size_t capacity);
[[gnu::weak]] void smith_import_cache_store(const char *kind, const char *key, const uint8_t *data, size_t size);
[[gnu::weak]] void smith_import_cache_count(const char *kind, bool hit);
int smith_cimport_file_state(const char *path, uint64_t *size, int64_t *mtime_ns);
int smith_cimport_file_hash(const char *path, uint64_t *out);
}

namespace {

constexpr char project_token[] = "\x01P";
constexpr char resource_token[] = "\x01R";

struct Folders {
    std::string project;  // the working folder
    std::string resource; // after -resource-dir, as given and resolved
    std::string resource_real;
};

std::string real_path(const std::string &path) {
    char resolved[PATH_MAX];
    if (path.empty() || ::realpath(path.c_str(), resolved) == nullptr) return path;
    return resolved;
}

Folders folders_of_request(const SmithCimportRequest *request) {
    Folders f;
    char here[PATH_MAX];
    if (::getcwd(here, sizeof here) != nullptr) f.project = here;
    std::vector<std::string> flags = lines(request->flags);
    for (size_t i = 0; i + 1 < flags.size(); i++)
        if (flags[i] == "-resource-dir") f.resource = flags[i + 1];
    f.resource_real = real_path(f.resource);
    return f;
}

// `path` with a folder of `f` at its start written as its token.
std::string tokened(const Folders &f, const std::string &path) {
    auto under = [&](const std::string &folder) {
        return folder.size() > 1 && path.compare(0, folder.size(), folder) == 0 &&
               (path.size() == folder.size() || path[folder.size()] == '/');
    };
    if (under(f.resource)) return resource_token + path.substr(f.resource.size());
    if (under(f.resource_real)) return resource_token + path.substr(f.resource_real.size());
    if (under(f.project)) return project_token + path.substr(f.project.size());
    return path;
}

std::string untokened(const Folders &f, const std::string &path) {
    if (path.compare(0, 2, resource_token) == 0) return f.resource + path.substr(2);
    if (path.compare(0, 2, project_token) == 0) return f.project + path.substr(2);
    return path;
}

bool cache_on() {
    return smith_import_cache_folder != nullptr && smith_import_cache_folder()[0] != '\0';
}

// The key of a request: its parts with the two folders as tokens. Empty when a folder of the request or a flag holds
// a token's first byte (it could not be told from one).
std::string cache_key(const SmithCimportRequest *request, const Folders &f) {
    auto plain = [](const char *text) { return text == nullptr || std::strchr(text, '\x01') == nullptr; };
    if (!plain(request->name) || !plain(request->folder) || !plain(request->flags)) return "";
    std::string material = "smith-cimport-words 1\n";
    material += request->name;
    material += request->quoted != 0 ? "\n\"\n" : "\n<\n";
    std::string folder = request->quoted != 0 && request->folder != nullptr ? request->folder : "";
    material += tokened(f, folder) + "\n";
    for (const std::string &flag : lines(request->flags))
        material += tokened(f, flag) + "\n";
    char key[33];
    smith_import_cache_key(material.data(), material.size(), key);
    return key;
}

// Every real file clang read for the header, as an absolute path (the source given as text is not one).
void files_read(const clang::SourceManager &sm, std::vector<std::string> *files) {
    std::set<std::string> seen;
    for (auto it = sm.fileinfo_begin(); it != sm.fileinfo_end(); ++it) {
        std::string path = real_path(it->first.getName().str());
        if (path.empty() || path[0] != '/') continue;
        seen.insert(path);
    }
    files->assign(seen.begin(), seen.end());
}

void put32(std::string *out, uint32_t v) {
    out->append(reinterpret_cast<const char *>(&v), 4);
}

void put64(std::string *out, uint64_t v) {
    out->append(reinterpret_cast<const char *>(&v), 8);
}

void put_text(std::string *out, const std::string &text) {
    put32(out, static_cast<uint32_t>(text.size()));
    *out += text;
}

struct Taker {
    const std::string &data;
    size_t at = 0;
    bool bad = false;
    bool take(void *out, size_t n) {
        if (bad || data.size() - at < n) return !(bad = true);
        std::memcpy(out, data.data() + at, n);
        at += n;
        return true;
    }
    uint32_t u32() {
        uint32_t v = 0;
        take(&v, 4);
        return v;
    }
    uint64_t u64() {
        uint64_t v = 0;
        take(&v, 8);
        return v;
    }
    std::string text() {
        uint32_t n = u32();
        if (bad || data.size() - at < n) {
            bad = true;
            return "";
        }
        std::string s = data.substr(at, n);
        at += n;
        return s;
    }
};

// The words with the file of each place row changed by `change`: the new names go at the end of the pool and the rows
// point there (the old text stays, unused), so no other offset moves.
template <typename Change>
std::vector<uint32_t> with_place_files(const uint32_t *w, size_t count, Change change) {
    std::vector<uint32_t> out(w, w + count);
    if (count < 25 || w[1] != count) return out;
    const char *pool = reinterpret_cast<const char *>(w) + w[2];
    std::string tail;
    std::map<std::string, uint32_t> added;
    uint32_t pool_size = w[3];
    for (uint32_t i = 0; i < w[24]; i++) {
        size_t row = w[23] + 6 * static_cast<size_t>(i);
        if (row + 6 > count) return std::vector<uint32_t>(w, w + count);
        std::string file = pool + w[row + 2];
        std::string changed = change(file);
        if (changed == file) continue;
        auto found = added.find(changed);
        if (found == added.end()) {
            found = added.emplace(changed, pool_size + static_cast<uint32_t>(tail.size())).first;
            tail += changed;
            tail += '\0';
        }
        out[row + 2] = found->second;
    }
    if (tail.empty()) return out;
    std::string whole(pool, w[3]);
    whole += tail;
    out.resize(w[2] / 4);
    out.resize(w[2] / 4 + (whole.size() + 3) / 4, 0);
    std::memcpy(reinterpret_cast<char *>(out.data()) + w[2], whole.data(), whole.size());
    out[1] = static_cast<uint32_t>(out.size());
    out[3] = static_cast<uint32_t>(whole.size());
    return out;
}

} // namespace

namespace {

// The entry of a request: the files read (each its tokened path, size, time and content hash), then the words with
// their place files tokened.
bool cache_load(const SmithCimportRequest *request, std::vector<uint32_t> *words) {
    if (!cache_on()) return false;
    Folders f = folders_of_request(request);
    std::string key = cache_key(request, f);
    if (key.empty()) return false;
    size_t size = smith_import_cache_load("h", key.c_str(), nullptr, 0);
    std::string data(size == SIZE_MAX ? 0 : size, '\0');
    if (size == SIZE_MAX ||
        smith_import_cache_load("h", key.c_str(), reinterpret_cast<uint8_t *>(data.data()), size) != size) {
        smith_import_cache_count("h", false);
        return false;
    }
    Taker r{data};
    uint32_t count = r.u32();
    std::vector<std::string> files;
    bool moved = false;
    for (uint32_t i = 0; i < count && !r.bad; i++) {
        std::string path = untokened(f, r.text());
        uint64_t size_was = r.u64(), time_was = r.u64(), low = r.u64(), high = r.u64();
        uint64_t size_now = 0;
        int64_t time_now = 0;
        if (r.bad || smith_cimport_file_state(path.c_str(), &size_now, &time_now) != 0) r.bad = true;
        if (r.bad || (size_now == size_was && static_cast<uint64_t>(time_now) == time_was)) {
            files.push_back(path);
            continue;
        }
        uint64_t hash[2] = {};
        if (smith_cimport_file_hash(path.c_str(), hash) != 0 || hash[0] != low || hash[1] != high) r.bad = true;
        moved = true;
        files.push_back(path);
    }
    uint32_t total = r.u32();
    std::vector<uint32_t> stored(r.bad ? 0 : total);
    if (!r.bad) r.take(stored.data(), static_cast<size_t>(total) * 4);
    if (r.bad || r.at != data.size() || total < 25) {
        smith_import_cache_count("h", false);
        return false;
    }
    *words = with_place_files(stored.data(), stored.size(), [&](const std::string &p) { return untokened(f, p); });
    smith_import_cache_count("h", true);
    if (moved) cache_store(request, words->data(), words->size(), files, INT64_MAX); // the new times: the next hit stats
    return true;
}

int64_t now_ns() {
    struct timespec t = {};
    clock_gettime(CLOCK_REALTIME, &t);
    return static_cast<int64_t>(t.tv_sec) * 1000000000LL + t.tv_nsec;
}

// Stores the words of a read that began at `started`: not when a file it read changed since then (its time is not
// before it), since the words may not be what the file holds now.
void cache_store(const SmithCimportRequest *request, const uint32_t *words, size_t count,
                 const std::vector<std::string> &files, int64_t started) {
    if (!cache_on() || count < 25 || words[16] != 0 || files.empty()) return;
    Folders f = folders_of_request(request);
    std::string key = cache_key(request, f);
    if (key.empty()) return;
    std::string data;
    put32(&data, static_cast<uint32_t>(files.size()));
    for (const std::string &path : files) {
        uint64_t size = 0;
        int64_t time = 0;
        uint64_t hash[2] = {};
        if (smith_cimport_file_state(path.c_str(), &size, &time) != 0 || time >= started ||
            smith_cimport_file_hash(path.c_str(), hash) != 0)
            return;
        put_text(&data, tokened(f, path));
        put64(&data, size);
        put64(&data, static_cast<uint64_t>(time));
        put64(&data, hash[0]);
        put64(&data, hash[1]);
    }
    std::vector<uint32_t> out = with_place_files(words, count, [&](const std::string &p) { return tokened(f, p); });
    put32(&data, static_cast<uint32_t>(out.size()));
    data.append(reinterpret_cast<const char *>(out.data()), out.size() * 4);
    smith_import_cache_store("h", key.c_str(), reinterpret_cast<const uint8_t *>(data.data()), data.size());
}

} // namespace
