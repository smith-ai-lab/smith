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
//   words[8..9]   structs: first word, count    (4 words each: name, first field, field count, flags)
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
//                 high = byte count). The enumerators of an anonymous enum, the object-like macros whose value clang
//                 folds to a constant, and the header's `static const` variables with a constant initializer.)
//
// Type kinds: 0 void; 1 bool; 2 integer (a = bits, b = 1 when signed); 3 floating (a = bits); 4 pointer (a = pointee);
// 5 array (a = element, b = count); 6 typedef (a = name, b = underlying); 7 struct (a = struct index); 8 function
// (a = return type, b = first parameter, c = parameter count; flags bit 2 = variadic); 9 unsupported (a = the spelling
// of the C type that has no smith equivalent: a union, `long double`, a bit-field); 11 string (smith's `Str`, the type
// of a string macro); 10 function pointer (a = the function type; smith has no pointer-to-function syntax, it is `Fn`
// applied to the function type). Type flags: 1 const, 2 restrict. A declaration that uses an unsupported type is still
// read, with that type, and is also in the not imported list with the reason: the use is what reports it. Struct flags:
// 1 opaque (no definition). Not imported kinds: 0 declaration, 1 attribute (name is the declaration's, reason the
// attribute's spelling).
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
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "clang/AST/APValue.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/Expr.h"
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

    void skip(uint32_t kind, const std::string &name, const std::string &reason) {
        not_imported.push_back({kind, text(name), text(reason)});
    }

    // The struct of a record, registered (and its fields read) the first time it is met.
    uint32_t record(const clang::RecordDecl *decl);
    // The type, or UINT32_MAX with `failure` set. `behind` is true under a pointer, where a struct that could not be
    // imported stays usable as an opaque one.
    uint32_t convert(clang::QualType type, bool behind);
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
    if (type->isInteger() && type->getKind() != B::Int128 && type->getKind() != B::UInt128) {
        uint32_t bits = static_cast<uint32_t>(context().getTypeSize(clang::QualType(type, 0)));
        return type_node(Integer, flags, bits, type->isSignedInteger() ? 1 : 0, 0);
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
    uint32_t first = static_cast<uint32_t>(parameters.size());
    std::vector<uint32_t> kinds;
    for (clang::QualType p : proto->param_types()) {
        uint32_t t = convert(p, false);
        if (t == bad) return bad;
        kinds.push_back(t);
    }
    for (uint32_t t : kinds)
        parameters.push_back({0, t});
    return type_node(Function, flags | (proto->isVariadic() ? 4U : 0U), result, first,
                     static_cast<uint32_t>(kinds.size()));
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
        return type_node(Typedef, flags, text(name->getName().str()), under, 0);
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
        if (decl->isUnion()) {
            fail("a union is not imported");
            return bad;
        }
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
    structs.push_back({text(name), 0, 0, 1});
    if (definition == nullptr) return index; // opaque
    std::string outer = failure;             // the declaration that met this struct keeps its own reason
    failure.clear();
    std::vector<std::array<uint32_t, 2>> read;
    for (const clang::FieldDecl *field : definition->fields()) {
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
    structs[index][3] = 0;
    for (auto &f : read)
        fields.push_back(f);
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
        params.push_back({text(decl->getParamDecl(i)->getName().str()), convert(proto->getParamType(i), false)});
    if (!failure.empty()) skip(0, name, failure);
    uint32_t first = static_cast<uint32_t>(parameters.size());
    for (auto &p : params)
        parameters.push_back(p);
    functions.push_back(
        {text(name), result, first, static_cast<uint32_t>(params.size()), proto->isVariadic() ? 1U : 0U});
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
            enum_items.push_back({text(item->getName().str()), parts[0], parts[1], parts[2]});
            count++;
        } else {
            constants.push_back({text(item->getName().str()), underlying, 0, parts[2], parts[0], parts[1]});
        }
    }
    if (named) enums.push_back({text(name), underlying, first, count});
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
            if (constant(macro.name, v->getType(), v->getInit(), why)) continue;
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
            if (r->isUnion()) {
                std::string name = r->getName().str();
                if (name.empty() && r->getTypedefNameForAnonDecl())
                    name = r->getTypedefNameForAnonDecl()->getName().str();
                if (r->isThisDeclarationADefinition()) skip(0, "union " + name, "a union is not imported");
                continue;
            }
            if (r->isThisDeclarationADefinition() || r->getDefinition() == nullptr) record(r);
        } else if (const auto *e = llvm::dyn_cast<clang::EnumDecl>(d)) {
            enumeration(e);
        } else if (const auto *v = llvm::dyn_cast<clang::VarDecl>(d)) {
            // A `static const` or `constexpr` variable with a constant initializer; other variables are not read.
            std::string why;
            if (v->getType().isConstQualified() && v->getInit() != nullptr && !v->getType()->isPointerType())
                constant(v->getName().str(), v->getType(), v->getInit(), why);
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
            if (value) found.push_back({info->getDefinitionLoc(), {name, info->isFunctionLike()}});
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

// Reads the header. Returns the words the result needs; when `capacity` is smaller nothing is written and the caller
// calls again with a buffer that big.
extern "C" size_t smith_cimport_read(const SmithCimportRequest *request, uint32_t *words, size_t capacity) {
    PlainAllocator plain;
    std::string header = request->name;
    std::vector<std::string> flags = lines(request->flags);
    std::string code = std::string("#include ") + (request->quoted != 0 ? "\"" : "<") + header +
                       (request->quoted != 0 ? "\"" : ">") + "\n";
    std::vector<std::string> args = {"-x", "c", "-std=gnu23", "-fno-color-diagnostics", "-ferror-limit=0"};
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
    Reader reader(unit != nullptr ? &unit->getASTContext() : nullptr);
    uint32_t error_at = 0;
    if (unit != nullptr && error.empty())
        reader.walk(unit->getASTContext().getTranslationUnitDecl(), macros, diagnostics.line_errors);
    if (!error.empty()) error_at = reader.text(error);

    // Lay the tables out: header, types, functions, structs, fields, parameters, not imported, then the pool.
    constexpr size_t head = 23;
    size_t at = head;
    size_t types_at = at, functions_at = types_at + reader.types.size() * 5,
           structs_at = functions_at + reader.functions.size() * 5;
    size_t fields_at = structs_at + reader.structs.size() * 4, parameters_at = fields_at + reader.fields.size() * 2;
    size_t skipped_at = parameters_at + reader.parameters.size() * 2,
           enums_at = skipped_at + reader.not_imported.size() * 3, items_at = enums_at + reader.enums.size() * 4,
           constants_at = items_at + reader.enum_items.size() * 4, pool_at = constants_at + reader.constants.size() * 6;
    size_t pool_words = (reader.pool.size() + 3) / 4, total = pool_at + pool_words;
    if (total > capacity || words == nullptr) return total;
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
    std::memcpy(reinterpret_cast<char *>(w) + pool_at * 4, reader.pool.data(), reader.pool.size());
    return total;
}
