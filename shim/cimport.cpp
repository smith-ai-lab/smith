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
//
// Type kinds: 0 void; 1 bool; 2 integer (a = bits, b = 1 when signed); 3 floating (a = bits); 4 pointer (a = pointee);
// 5 array (a = element, b = count); 6 typedef (a = name, b = underlying); 7 struct (a = struct index); 8 function
// (a = return type, b = first parameter, c = parameter count; flags bit 2 = variadic); 9 unsupported (a = the spelling
// of the C type that has no smith equivalent: a union, `long double`, a bit-field); 10 function pointer (a = the
// function type; smith has no pointer-to-function syntax, it is `Fn` applied to the function type). Type flags: 1
// const, 2 restrict. A declaration that uses an unsupported type is still read, with that type, and is also in the not
// imported list with the reason: the use is what reports it. Struct flags: 1 opaque (no definition). Not imported
// kinds: 0 declaration, 1 attribute (name is the declaration's, reason the attribute's spelling).
//
// Memory: the caller's buffer. When it is too small the call returns the words needed and the caller calls again.
#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/Type.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Lex/HeaderSearch.h"
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

namespace {

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
    FnPointer
};

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
    void walk(const clang::TranslationUnitDecl *unit);
};

constexpr uint32_t bad = UINT32_MAX;

bool from_header(const clang::Decl *decl, const clang::SourceManager &sm) {
    if (decl->isImplicit()) return false;
    clang::PresumedLoc loc = sm.getPresumedLoc(decl->getLocation());
    if (!loc.isValid()) return false;
    return loc.getFilename()[0] != '<' && std::strcmp(loc.getFilename(), "smith_cimport.c") != 0;
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

void Reader::walk(const clang::TranslationUnitDecl *unit) {
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
        }
    }
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
    void HandleDiagnostic(clang::DiagnosticsEngine::Level level, const clang::Diagnostic &info) override {
        clang::DiagnosticConsumer::HandleDiagnostic(level, info);
        if (level < clang::DiagnosticsEngine::Error || !first_error.empty()) return;
        llvm::SmallString<256> text;
        info.FormatDiagnostic(text);
        first_error = text.str().str();
    }
};

} // namespace

// Reads the header. Returns the words the result needs; when `capacity` is smaller nothing is written and the caller
// calls again with a buffer that big.
extern "C" size_t smith_cimport_read(const SmithCimportRequest *request, uint32_t *words, size_t capacity) {
    std::string header = request->name;
    std::vector<std::string> flags = lines(request->flags);
    std::string code = std::string("#include ") + (request->quoted != 0 ? "\"" : "<") + header +
                       (request->quoted != 0 ? "\"" : ">") + "\n";
    std::vector<std::string> args = {"-x", "c", "-std=gnu23", "-fno-color-diagnostics", "-fsyntax-only"};
    if (request->quoted != 0 && request->folder != nullptr && request->folder[0] != '\0') {
        args.push_back("-iquote");
        args.push_back(request->folder);
    }
    for (const std::string &f : flags)
        args.push_back(f);
    Capture diagnostics;
    std::unique_ptr<clang::ASTUnit> unit = clang::tooling::buildASTFromCodeWithArgs(
        code, args, "smith_cimport.c", "smith-cimport", std::make_shared<clang::PCHContainerOperations>(),
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
    if (unit != nullptr && error.empty()) reader.walk(unit->getASTContext().getTranslationUnitDecl());
    if (!error.empty()) error_at = reader.text(error);

    // Lay the tables out: header, types, functions, structs, fields, parameters, not imported, then the pool.
    constexpr size_t head = 17;
    size_t at = head;
    size_t types_at = at, functions_at = types_at + reader.types.size() * 5,
           structs_at = functions_at + reader.functions.size() * 5;
    size_t fields_at = structs_at + reader.structs.size() * 4, parameters_at = fields_at + reader.fields.size() * 2;
    size_t skipped_at = parameters_at + reader.parameters.size() * 2,
           pool_at = skipped_at + reader.not_imported.size() * 3;
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
    std::memcpy(reinterpret_cast<char *>(w) + pool_at * 4, reader.pool.data(), reader.pool.size());
    return total;
}
