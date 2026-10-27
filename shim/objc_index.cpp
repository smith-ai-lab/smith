// The index of an Objective-C header (issue #318, skill smith-lang-objc): what `import <AppKit/AppKit.h>;` in a .smith
// file reads. The hook (shim/hook.cpp) runs `smith cc -x objective-c -fsyntax-only` on a file that imports the header
// and sets SMITH_OBJC_INDEX to a folder; this plugin, which clang adds to every frontend action, then writes there:
//   - types.h: plain C for everything but the methods (a typedef of an opaque struct for each class and protocol, the
//     records and the scalar typedefs of the frameworks, the enum constants, the functions and the globals), each
//     named `objc_<name>`, so `objc::NSRect` is `objc_NSRect` and the file's C code is not touched by any of it;
//   - methods.idx: one tab-separated line per class, protocol and method, which the lowering (runtime/lang_objc.c)
//     writes the typed wrappers of the classes a file names from.
// Both are build outputs under .smith/objc/, never committed. The index is made with the SDK the build uses.
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Type.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"

#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace clang;

namespace {

class Writer {
  public:
    explicit Writer(ASTContext &context)
        : context(context), sources(context.getSourceManager()), policy(context.getPrintingPolicy()) {
        policy.SuppressTagKeyword = false;
        arm = context.getTargetInfo().getTriple().getArch() != llvm::Triple::x86_64;
    }

    void run() {
        for (const Decl *decl : context.getTranslationUnitDecl()->decls())
            collect(decl);
        for (const Decl *decl : context.getTranslationUnitDecl()->decls())
            emit(decl);
    }

    // Whether the framework the header names declares classes or protocols of its own. Network.framework's headers
    // are C and are read in Objective-C mode with Foundation's classes behind them: those are not its API.
    bool sawObjc = false;
    std::string framework; // SMITH_OBJC_FRAMEWORK
    std::string forward; // typedefs of the opaque structs
    std::string records; // the records' definitions, each after the ones it holds
    std::string aliases; // typedefs of scalars, records, enums and objects
    std::string enums;   // the constants
    std::string symbols; // functions and globals

  private:
    ASTContext &context;
    SourceManager &sources;
    PrintingPolicy policy;
    bool arm = true;
    std::set<std::string> symbolNames;
    std::set<std::string> declared;              // the names in `forward`
    std::map<const Decl *, std::string> named;   // a record's name: its typedef's or its tag
    std::map<const Decl *, std::string> started; // a record to its spelling, "" while it is read or when it has none

    // ---- Which declarations are the frameworks' own

    bool inFramework(const Decl *decl) {
        SourceLocation at = sources.getExpansionLoc(decl->getLocation());
        if (at.isInvalid()) return false;
        llvm::StringRef file = sources.getFilename(at);
        return file.contains(".framework/") || file.contains("/usr/include/objc/");
    }

    void collect(const Decl *decl) {
        const auto *alias = dyn_cast<TypedefNameDecl>(decl);
        if (alias == nullptr || !inFramework(alias)) return;
        const auto *record = alias->getUnderlyingType()->getAsRecordDecl();
        if (record != nullptr && named.count(record->getCanonicalDecl()) == 0)
            named[record->getCanonicalDecl()] = alias->getName().str();
    }

    // ---- Types as C spells them

    void declare(const std::string &name, const char *word = "struct") {
        if (declared.insert(name).second)
            forward += std::string("typedef ") + word + " objc_" + name + " objc_" + name + ";\n";
    }

    static std::string object(const std::string &name) {
        return "objc_" + name + " *";
    }

    std::string objectType(const ObjCObjectPointerType *pointer) {
        if (pointer->isObjCClassType() || pointer->isObjCQualifiedClassType()) return "smith_objc_class";
        if (const ObjCInterfaceDecl *interface = pointer->getInterfaceDecl()) {
            declare(interface->getName().str());
            return object(interface->getName().str());
        }
        if (pointer->getNumProtocols() == 1) {
            std::string name = (*pointer->qual_begin())->getName().str();
            declare(name);
            return object(name);
        }
        return "smith_objc_id";
    }

    std::string recordName(const RecordDecl *record) {
        auto found = named.find(record->getCanonicalDecl());
        if (found != named.end()) return found->second;
        if (record->getIdentifier() != nullptr) return record->getName().str();
        return "";
    }

    // The fields of a record, or false when C cannot hold them the same (a bit-field, an unnamed member).
    bool fieldsOf(const RecordDecl *record, std::string &out) {
        for (const FieldDecl *field : record->fields()) {
            if (field->isBitField() || field->getName().empty()) return false;
            QualType type = field->getType();
            std::string suffix;
            if (const auto *array = context.getAsConstantArrayType(type)) {
                suffix = "[" + std::to_string(array->getSize().getZExtValue()) + "]";
                type = array->getElementType();
            }
            std::string spelled = spell(type);
            if (spelled.empty()) return false;
            out += "    " + spelled + " " + field->getName().str() + suffix + ";\n";
        }
        return true;
    }

    std::string recordType(const RecordDecl *record) {
        const RecordDecl *definition = record->getDefinition();
        std::string name = recordName(record);
        if (definition == nullptr || name.empty()) return "";
        auto known = started.find(definition->getCanonicalDecl());
        if (known != started.end()) return known->second;
        started[definition->getCanonicalDecl()] = ""; // a record that holds itself has no C spelling
        const char *word = definition->isUnion() ? "union" : "struct";
        declare(name, word);
        std::string body;
        if (!fieldsOf(definition, body)) return "";
        records += "#ifndef SMITH_OBJC_R_" + name + "\n#define SMITH_OBJC_R_" + name + "\n" + word + " objc_" + name +
                   " {\n" + body + "};\n#endif\n"; // a file that imports two headers includes two of these
        started[definition->getCanonicalDecl()] = "objc_" + name;
        return started[definition->getCanonicalDecl()];
    }

    std::string builtinType(QualType type) {
        if (type->isBooleanType()) return "_Bool";
        return type.getUnqualifiedType().getAsString(policy);
    }

    // The C spelling of a type, or "" when C has none (a block, a template).
    std::string spell(QualType type) {
        QualType canonical = type.getNonReferenceType().getCanonicalType();
        const Type *kind = canonical.getTypePtr();
        std::string qualifier = canonical.isConstQualified() ? "const " : "";
        if (canonical->isObjCSelType()) return "smith_objc_sel";
        if (isa<BuiltinType>(kind)) return qualifier + builtinType(canonical);
        if (const auto *enumeration = dyn_cast<EnumType>(kind))
            return qualifier + spell(enumeration->getDecl()->getIntegerType());
        if (const auto *pointer = dyn_cast<ObjCObjectPointerType>(kind)) return objectType(pointer);
        if (isa<BlockPointerType>(kind)) return "void *";
        if (const auto *pointer = dyn_cast<PointerType>(kind)) {
            QualType pointee = pointer->getPointeeType();
            if (pointee->isFunctionType()) return "void *";
            std::string inner = spell(pointee);
            return inner.empty() ? "" : inner + (inner.back() == '*' ? "*" : " *");
        }
        if (const auto *record = dyn_cast<RecordType>(kind)) {
            std::string name = recordType(record->getDecl());
            return name.empty() ? "" : qualifier + name;
        }
        if (isa<VectorType>(kind) || isa<ComplexType>(kind))
            return qualifier + canonical.getUnqualifiedType().getAsString(policy);
        return "";
    }

    // ---- The declarations

    void emit(const Decl *decl) {
        if (!inFramework(decl)) return;
        if (const auto *interface = dyn_cast<ObjCInterfaceDecl>(decl)) {
            if (interface->isThisDeclarationADefinition()) classLine(interface);
        } else if (const auto *protocol = dyn_cast<ObjCProtocolDecl>(decl)) {
            if (protocol->isThisDeclarationADefinition()) protocolLine(protocol);
        } else if (const auto *category = dyn_cast<ObjCCategoryDecl>(decl)) {
            categoryMethods(category);
        } else if (const auto *alias = dyn_cast<TypedefNameDecl>(decl)) {
            aliasLine(alias);
        } else if (const auto *enumeration = dyn_cast<EnumDecl>(decl)) {
            enumLine(enumeration);
        } else if (const auto *function = dyn_cast<FunctionDecl>(decl)) {
            functionLine(function);
        } else if (const auto *variable = dyn_cast<VarDecl>(decl)) {
            variableLine(variable);
        }
    }

    static std::string joined(const ObjCList<ObjCProtocolDecl> &protocols) {
        std::string out;
        for (const ObjCProtocolDecl *protocol : protocols)
            out += (out.empty() ? "" : ",") + protocol->getName().str();
        return out;
    }

    struct Block {
        std::string head;    // the `c` or `p` line, once the declaration is seen
        std::string adopted; // `a` lines: protocols a category adds
        std::string methods;
    };
    std::map<std::string, Block> classes; // the index groups a class's lines, its categories' included
    std::map<std::string, Block> protocols;

    void ownClasses(const Decl *decl) {
        llvm::StringRef file = sources.getFilename(sources.getExpansionLoc(decl->getLocation()));
        if (file.contains("/" + framework + ".framework/")) sawObjc = true;
    }

    void classLine(const ObjCInterfaceDecl *interface) {
        ownClasses(interface);
        std::string name = interface->getName().str();
        declare(name);
        const ObjCInterfaceDecl *super = interface->getSuperClass();
        Block &block = classes[name];
        block.head = "c\t" + name + "\t" + (super != nullptr ? super->getName().str() : "-") + "\t" +
                     joined(interface->getReferencedProtocols()) + "\n";
        methodLines(interface, name, block.methods);
    }

    void protocolLine(const ObjCProtocolDecl *protocol) {
        ownClasses(protocol);
        std::string name = protocol->getName().str();
        declare(name);
        Block &block = protocols[name];
        block.head = "p\t" + name + "\t" + joined(protocol->getReferencedProtocols()) + "\n";
        methodLines(protocol, name, block.methods);
    }

    void categoryMethods(const ObjCCategoryDecl *category) {
        const ObjCInterfaceDecl *interface = category->getClassInterface();
        if (interface == nullptr) return;
        std::string name = interface->getName().str();
        Block &block = classes[name];
        std::string adopted = joined(category->getReferencedProtocols());
        if (!adopted.empty()) block.adopted += "a\t" + name + "\t" + adopted + "\n";
        methodLines(category, name, block.methods);
    }

  public:
    // methods.idx, grouped: a block begins with its `c` or `p` line (a class a header only extends has `c name - -`).
    std::string index() {
        std::string out = std::string("objc\t") + (sawObjc ? "1" : "0") + "\n";
        for (auto &[name, block] : classes)
            out += (block.head.empty() ? "c\t" + name + "\t-\t\n" : block.head) + block.adopted + block.methods;
        for (auto &[name, block] : protocols)
            out += block.head + block.methods;
        return out;
    }

  private:
    static std::string selectorName(const ObjCMethodDecl *method) {
        std::string name;
        Selector selector = method->getSelector();
        for (unsigned i = 0; i < std::max(1U, selector.getNumArgs()); i++)
            name += (i == 0 ? "" : "_") + selector.getNameForSlot(i).str();
        return name;
    }

    bool owned(const ObjCMethodDecl *method) {
        switch (method->getMethodFamily()) {
        case OMF_alloc:
        case OMF_copy:
        case OMF_mutableCopy:
        case OMF_new:
            return true;
        default:
            return method->hasAttr<NSReturnsRetainedAttr>();
        }
    }

    // The flags: o (the result is +1), s (x86_64 returns it through memory: objc_msgSend_stret), f
    // (objc_msgSend_fpret).
    std::string flags(const ObjCMethodDecl *method, QualType result) {
        std::string out = owned(method) ? "o" : "";
        if (!arm && result->isRecordType() && context.getTypeSize(result) > 128) out += "s";
        if (!arm && result->isSpecificBuiltinType(BuiltinType::LongDouble)) out += "f";
        return out.empty() ? "-" : out;
    }

    void methodLines(const ObjCContainerDecl *container, const std::string &owner, std::string &sink) {
        for (const ObjCMethodDecl *method : container->methods()) {
            if (method->isUnavailable() || method->isVariadic()) continue;
            QualType result = method->getReturnType();
            std::string line = method->hasRelatedResultType() ? "@self" : spell(result);
            if (line.empty()) continue;
            std::string parameters;
            for (const ParmVarDecl *parameter : method->parameters()) {
                std::string spelled = spell(parameter->getType());
                if (spelled.empty()) {
                    parameters = "?";
                    break;
                }
                parameters += "\t" + spelled;
            }
            if (parameters == "?") continue;
            sink += std::string("m\t") + owner + "\t" + (method->isInstanceMethod() ? "-" : "+") + "\t" +
                    selectorName(method) + "\t" + method->getSelector().getAsString() + "\t" + flags(method, result) +
                    "\t" + line + parameters + "\n";
        }
    }

    void aliasLine(const TypedefNameDecl *alias) {
        std::string target = spell(alias->getUnderlyingType());
        if (target.empty() || alias->getName() == "instancetype") return;
        std::string name = alias->getName().str();
        const RecordDecl *record = alias->getUnderlyingType()->getAsRecordDecl();
        if (record != nullptr && recordName(record) == name) return; // the record's own typedef
        if (!declared.count(name)) aliases += "typedef " + target + " objc_" + name + ";\n";
    }

    void enumLine(const EnumDecl *enumeration) {
        if (!enumeration->isCompleteDefinition()) return;
        std::string type = spell(enumeration->getIntegerType());
        if (type.empty()) return;
        std::string body;
        for (const EnumConstantDecl *constant : enumeration->enumerators()) {
            llvm::SmallString<32> digits;
            constant->getInitVal().toString(digits, 10);
            body += "    objc_" + constant->getName().str() + " = " + std::string(digits) +
                    (enumeration->getIntegerType()->isUnsignedIntegerType() ? "ULL" : "") + ",\n";
        }
        if (body.empty()) return;
        std::string guard = "SMITH_OBJC_E_" + (*enumeration->enumerator_begin())->getName().str();
        enums += "#ifndef " + guard + "\n#define " + guard + "\nenum : " + type + " {\n" + body + "};\n#endif\n";
    }

    void functionLine(const FunctionDecl *function) {
        if (function->isInlined() || function->isVariadic() || function->hasBody() ||
            function->getStorageClass() == SC_Static || function->getName().empty() || function->isUnavailable())
            return;
        std::string signature = spell(function->getReturnType());
        if (signature.empty()) return;
        std::string parameters;
        for (const ParmVarDecl *parameter : function->parameters()) {
            std::string spelled = spell(parameter->getType());
            if (spelled.empty()) return;
            parameters += (parameters.empty() ? "" : ", ") + spelled;
        }
        std::string name = function->getName().str();
        if (!symbolNames.insert(name).second) return; // declared again, as another header spells it
        symbols += "extern " + signature + " objc_" + name + "(" + (parameters.empty() ? "void" : parameters) +
                   ") __asm__(\"_" + name + "\");\n"; // Mach-O: an asm label is the symbol as it is
    }

    void variableLine(const VarDecl *variable) {
        if (variable->hasInit() || !variable->hasExternalStorage() || variable->getName().empty() ||
            variable->isUnavailable())
            return;
        std::string type = spell(variable->getType());
        if (type.empty()) return;
        std::string name = variable->getName().str();
        if (!symbolNames.insert(name).second) return;
        symbols += "extern " + type + " objc_" + name + " __asm__(\"_" + name + "\");\n";
    }
};

const char *const preamble =
    "// Generated by smith from an Objective-C header (smith-objc-index). Do not edit.\n"
    "#pragma once\n"
    "#pragma clang system_header\n" // generated: no check or warning of the project reads it
    "typedef void *smith_objc_id;\n"
    "typedef struct smith_objc_class_ *smith_objc_class;\n"
    "typedef struct smith_objc_selector_ *smith_objc_sel;\n"
    "extern void smith_objc_send(void) __asm__(\"_objc_msgSend\");\n"
    "extern void smith_objc_send_stret(void) __asm__(\"_objc_msgSend_stret\");\n"
    "extern void smith_objc_send_fpret(void) __asm__(\"_objc_msgSend_fpret\");\n"
    "extern smith_objc_sel smith_objc_selector(const char *) __asm__(\"_sel_registerName\");\n"
    "extern smith_objc_class smith_objc_class_named(const char *) __asm__(\"_objc_getClass\");\n";

bool write(const std::string &folder, const char *name, const std::string &text) {
    std::string path = folder + "/" + name;
    std::string temporary = path + ".tmp" + std::to_string(::getpid());
    {
        std::error_code failure;
        llvm::raw_fd_ostream out(temporary, failure);
        if (failure) return false;
        out << text;
    }
    return !llvm::sys::fs::rename(temporary, path);
}

class Consumer : public ASTConsumer {
  public:
    explicit Consumer(std::string folder) : folder(std::move(folder)) {}

    void HandleTranslationUnit(ASTContext &context) override {
        if (context.getDiagnostics().hasErrorOccurred()) return;
        Writer writer(context);
        if (const char *framework = std::getenv("SMITH_OBJC_FRAMEWORK")) writer.framework = framework;
        writer.run();
        std::string header =
            std::string(preamble) + writer.forward + writer.records + writer.aliases + writer.enums + writer.symbols;
        std::string index = writer.index();
        if (writer.sawObjc && !write(folder, "types.h", header)) return;
        write(folder, "methods.idx", index); // last: its presence says the folder is complete
    }

  private:
    std::string folder;
};

struct ObjcIndexPlugin final : PluginASTAction {
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &, StringRef) override {
        const char *folder = std::getenv("SMITH_OBJC_INDEX");
        if (folder == nullptr) return std::make_unique<ASTConsumer>(); // never null: the multiplexer calls it
        return std::make_unique<Consumer>(folder);
    }
    bool ParseArgs(const CompilerInstance &, const std::vector<std::string> &) override {
        return true;
    }
    ActionType getActionType() override {
        return AddAfterMainAction;
    }
};

const FrontendPluginRegistry::Add<ObjcIndexPlugin> plugin("smith-objc-index", "the index of an Objective-C header");

} // namespace
