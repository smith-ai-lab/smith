// A C interface to clang-tidy's own main, linked into smith as `smith lint`.
//
// clangTidyMain is the function clang-tidy's main calls (clang-tidy/tool/ClangTidyMain.cpp), so every flag, .clang-tidy
// file, check, fix and diagnostic is clang-tidy's own. See tool.h for how its initializers and its resource directory
// are handled.
#include "clang-tidy/tool/ClangTidyMain.h"
#include "tool.h"

#ifdef SMITH_HOOK
#include "clang-tidy/ClangTidyModule.h"
#include "clang-tidy/readability/IdentifierNamingCheck.h"
#include "effects.h"
#include "idiom.h"
#include "mem.h"
#include "names.h"
#include "preprocessor.h"
#endif

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"

#include <cstddef>
#include <vector>

extern "C" void smith_affine_disable();
extern "C" void smith_effects_disable();
#ifdef SMITH_HOOK
// shim/hook.cpp: makes every translation unit with a .smith main file, clang-tidy's included, offer its tokens to the
// lowering and show its diagnostics (clang-tidy's own, fix-its included) with the names as written.
extern "C" void smith_hook_install();
#endif

#ifdef SMITH_HOOK
// ---- Checks see the names the source wrote
//
// The lowering makes a module's file-scope names `geometry_box_Box`, and the checks read what the AST holds. A check's
// finding is reported from an engine of clang-tidy's own, not the compiler's, so two things are done once per
// translation unit (a .smith one; any other is left as it is):
//   - the engine's findings go through the consumer that shows the compiler's diagnostics (smith::presentThrough,
//     shim/hook.cpp): names as written, the copy of a prototype that the lowering puts above a module dropped, a note
//     into a generated header at the module's source;
//   - readability-identifier-naming judges the name the source wrote (`Box`), not the lowering's: SmithNaming below.
// Both are made when a check is created (clang-tidy creates its checks for each translation unit, after the
// preprocessor, so after the hook), by wrapping the factory of every check in a module registered after clang-tidy's.
namespace clang::tidy {
namespace {

using Naming = readability::IdentifierNamingCheck;

// IdentifierNamingCheck is final and its decisions are private, so SmithNaming holds one and asks it. A pointer to a
// private member is named through an explicit instantiation, which C++ does not check for access.
struct DeclFn {
    using type = std::optional<RenamerClangTidyCheck::FailureInfo> (Naming::*)(const NamedDecl *,
                                                                               const SourceManager &) const;
};
struct MacroFn {
    using type = std::optional<RenamerClangTidyCheck::FailureInfo> (Naming::*)(const Token &,
                                                                               const SourceManager &) const;
};
struct EngineFn { // the engine the checks of a context report from
    using type = DiagnosticsEngine *ClangTidyContext::*;
};
template <typename Tag, typename Tag::type Member> struct Reach {
    friend typename Tag::type reach(Tag) {
        return Member;
    }
};
DeclFn::type reach(DeclFn);
MacroFn::type reach(MacroFn);
EngineFn::type reach(EngineFn);
template struct Reach<EngineFn, &ClangTidyContext::DiagEngine>;
template struct Reach<DeclFn, &Naming::getDeclFailureInfo>;
template struct Reach<MacroFn, &Naming::getMacroFailureInfo>;

// readability-identifier-naming for a .smith file. A file-scope name of a module is judged as the source wrote it, by
// the project's own rules: `area` and `Box`, never `geometry_box_area`, so the finding, its message and its fix-it
// (which replaces the token the source has, not the one the lowering made) are about the text. The name is the
// lowering's own table (smith::namesOf: the names its last parse of this file made), not a guess at underscores, and
// the declaration has it only for the one question asked of the inner check: nothing else in the AST changes.
class SmithNaming final : public RenamerClangTidyCheck {
  public:
    SmithNaming(StringRef Name, ClangTidyContext *Context)
        : RenamerClangTidyCheck(Name, Context), inner(Name, Context) {}

    void storeOptions(ClangTidyOptions::OptionMap &Opts) override {
        inner.storeOptions(Opts);
    }

  protected:
    std::optional<FailureInfo> getDeclFailureInfo(const NamedDecl *Decl, const SourceManager &SM) const override {
        // A name that is not a simple identifier (an Objective-C selector, an operator, a conversion) has no identifier
        // to ask for, and Objective-C naming is not smith's to enforce: ivars, properties and methods are skipped.
        if (!Decl->getDeclName().isIdentifier() || isObjC(Decl)) return std::nullopt;
        if (isTuplePart(Decl) || isEnumPart(Decl)) return std::nullopt;
        const smith::WrittenName *written = writtenName(Decl, SM);
        if (written == nullptr || written->member.empty()) return (inner.*reach(DeclFn{}))(Decl, SM);
        // The first declaration of an exported function is the lowering's copy of its prototype, which is in no file
        // the user's rules apply to; the definition is the text the user wrote, so it is the one judged. (The finding
        // is made at the copy's place, which smith::presentThrough's consumer moves to the token the copy was made
        // from.)
        const NamedDecl *judged = definitionOf(Decl);
        auto *mutableDecl = const_cast<NamedDecl *>(judged);
        const DeclarationName lowered = mutableDecl->getDeclName();
        mutableDecl->setDeclName(DeclarationName(&Decl->getASTContext().Idents.get(judgedName(written->member))));
        std::optional<FailureInfo> failure = (inner.*reach(DeclFn{}))(judged, SM);
        mutableDecl->setDeclName(lowered);
        return failure;
    }

    std::optional<FailureInfo> getMacroFailureInfo(const Token &MacroNameTok, const SourceManager &SM) const override {
        return (inner.*reach(MacroFn{}))(MacroNameTok, SM);
    }

    DiagInfo getDiagInfo(const NamingCheckId &ID, const NamingCheckFailure &Failure) const override {
        std::string name = ID.second.str();
        if (names != nullptr) {
            const smith::WrittenName *written = names->find(ID.second);
            if (written != nullptr && !written->member.empty()) name = judgedName(written->member);
        }
        return DiagInfo{"invalid case style for %0 '%1'",
                        [kind = Failure.Info.KindName, name](DiagnosticBuilder &Diag) { Diag << kind << name; }};
    }

  private:
    // A generic instance is written `push_array<f64>`; the name the rules judge is the generic's, `push_array`.
    static StringRef judgedName(StringRef member) {
        return member.substr(0, member.find('<'));
    }

    // A tuple `struct { i32, f32 }` is lowered to a struct named for its fields (`smith_tuple_i32__f32`) whose fields
    // are `_0`, `_1`, and a slice `T xs[:]` to `smith_array_i32_n` with `data` and `count`: names the lowering made and
    // the program never wrote, so no rule of naming applies to them (nor to `smith_field`, the struct of a `Field`).
    static bool isObjC(const NamedDecl *Decl) {
        return isa<ObjCContainerDecl, ObjCMethodDecl, ObjCIvarDecl, ObjCPropertyDecl, ObjCCompatibleAliasDecl>(Decl) ||
               isa<ObjCContainerDecl>(Decl->getDeclContext());
    }

    static bool isTuplePart(const NamedDecl *Decl) {
        auto made = [](StringRef name) {
            return name.starts_with("smith_tuple_") || name.starts_with("smith_array_") ||
                   name.starts_with("smith_enum_") || name == "smith_field";
        };
        if (const auto *record = dyn_cast<RecordDecl>(Decl)) return made(record->getName());
        if (const auto *field = dyn_cast<FieldDecl>(Decl)) return made(field->getParent()->getName());
        return false;
    }

    // A payload enum (`typedef enum { Circle circle; empty; } Shape;`) is lowered to a struct marked `smith::enum` that
    // holds the enum `Shape_tag` with the constants `Shape_tag_circle`, and the member `tag`: the lowering's own names.
    static bool isEnumRecord(const Decl *decl) {
        const auto *record = dyn_cast_or_null<RecordDecl>(decl);
        if (record == nullptr) return false;
        for (const Decl *each : record->redecls())
            for (const auto *attr : each->specific_attrs<AnnotateAttr>())
                if (attr->getAnnotation() == "smith::enum") return true;
        return false;
    }

    static bool isEnumPart(const NamedDecl *Decl) {
        if (const auto *constant = dyn_cast<EnumConstantDecl>(Decl))
            return isEnumPart(cast<EnumDecl>(constant->getDeclContext()));
        if (const auto *tag = dyn_cast<EnumDecl>(Decl))
            return isEnumRecord(dyn_cast<RecordDecl>(tag->getLexicalDeclContext()));
        if (const auto *field = dyn_cast<FieldDecl>(Decl))
            return field->getName() == "tag" && isEnumRecord(field->getParent());
        return false;
    }

    static const NamedDecl *definitionOf(const NamedDecl *Decl) {
        if (const auto *function = dyn_cast<FunctionDecl>(Decl)) {
            if (const FunctionDecl *definition = function->getDefinition()) return definition;
        } else if (const auto *variable = dyn_cast<VarDecl>(Decl)) {
            if (const VarDecl *definition = variable->getDefinition()) return definition;
        }
        return Decl;
    }

    // What the lowering made of this declaration's name, null for a name it did not make.
    const smith::WrittenName *writtenName(const NamedDecl *Decl, const SourceManager &SM) const {
        const IdentifierInfo *identifier = Decl->getIdentifier();
        if (identifier == nullptr || !identifier->getName().contains('_')) return nullptr;
        OptionalFileEntryRef main = SM.getFileEntryRefForID(SM.getMainFileID());
        if (!main || !main->getName().ends_with(".smith")) return nullptr;
        names = smith::namesOf(main->getName().str());
        return names != nullptr ? names->find(identifier->getName()) : nullptr;
    }

    Naming inner;
    mutable std::shared_ptr<const smith::FileNames> names;
};

// Makes the findings of the translation unit's engine show names as written, when it is a .smith file.
// clang-tidy's own findings only: clangd shows its checks' findings through its own diagnostics and the layer of
// shim/lsp.cpp, which writes the names as the source did.
bool presenting = false;

void attach(ClangTidyContext *Context) {
    if (!presenting) return;
    if (DiagnosticsEngine *engine = Context->*reach(EngineFn{})) smith::presentThrough(*engine);
}

class SmithModule final : public ClangTidyModule {
  public:
    void addCheckFactories(ClangTidyCheckFactories &Factories) override {
        std::vector<std::pair<std::string, ClangTidyCheckFactories::CheckFactory>> all;
        for (const auto &entry : Factories)
            all.emplace_back(entry.getKey().str(), entry.getValue());
        for (auto &[name, make] : all) {
            Factories.registerCheckFactory(name, [make = make](StringRef Name, ClangTidyContext *Context) {
                attach(Context);
                return make(Name, Context);
            });
        }
        Factories.registerCheckFactory("smith-effects", [](StringRef Name, ClangTidyContext *Context) {
            attach(Context);
            return std::make_unique<smith_effects::Effects>(Name, Context);
        });
        Factories.registerCheckFactory("smith-preprocessor", [](StringRef Name, ClangTidyContext *Context) {
            attach(Context);
            return std::make_unique<smith_preprocessor::PreprocessorCheck>(Name, Context);
        });
        Factories.registerCheckFactory("smith-mem", [](StringRef Name, ClangTidyContext *Context) {
            attach(Context);
            return std::make_unique<smith_mem::MemCheck>(Name, Context);
        });
        Factories.registerCheckFactory("smith-sized-types", [](StringRef Name, ClangTidyContext *Context) {
            attach(Context);
            return std::make_unique<smith_idiom::SizedTypesCheck>(Name, Context);
        });
        Factories.registerCheckFactory("smith-module-library", [](StringRef Name, ClangTidyContext *Context) {
            attach(Context);
            return std::make_unique<smith_idiom::ModuleLibraryCheck>(Name, Context);
        });
        Factories.registerCheckFactory("smith-bounded-loop", [](StringRef Name, ClangTidyContext *Context) {
            attach(Context);
            return std::make_unique<smith_idiom::BoundedLoopCheck>(Name, Context);
        });
        Factories.registerCheckFactory("smith-c23", [](StringRef Name, ClangTidyContext *Context) {
            attach(Context);
            return std::make_unique<smith_idiom::C23Check>(Name, Context);
        });
        Factories.registerCheckFactory("smith-constexpr", [](StringRef Name, ClangTidyContext *Context) {
            attach(Context);
            return std::make_unique<smith_idiom::ConstexprCheck>(Name, Context);
        });
        Factories.registerCheckFactory("smith-module", [](StringRef Name, ClangTidyContext *Context) {
            attach(Context);
            return std::make_unique<smith_idiom::ModuleCheck>(Name, Context);
        });
        Factories.registerCheckFactory("smith-designated-init", [](StringRef Name, ClangTidyContext *Context) {
            attach(Context);
            return std::make_unique<smith_idiom::DesignatedInitCheck>(Name, Context);
        });
        Factories.registerCheckFactory("readability-identifier-naming", [](StringRef Name, ClangTidyContext *Context) {
            attach(Context);
            return std::make_unique<SmithNaming>(Name, Context);
        });
    }
};

} // namespace
} // namespace clang::tidy

// Registers the module after the ones clang-tidy has registered (by now), so that it wraps their checks. clang-tidy's
// main and clangd's built-in checks (clangd creates them from the same registry) both ask for it; `present` is whether
// the findings of the checks go through smith::presentThrough (clang-tidy's do, clangd's do not).
extern "C" void smith_tidy_names_register(bool present) {
    clang::tidy::presenting = present;
    static const llvm::Registry<clang::tidy::ClangTidyModule>::Add<clang::tidy::SmithModule> smith_module(
        "smith-lang", "checks see the names a .smith file wrote");
}
#else
extern "C" void smith_tidy_names_register(bool) {}
#endif

// Runs clang-tidy on argv[1 .. argc), as argv[0], <folder>/bin/clang-tidy, in clang's folder. Returns
// its exit status.
extern "C" int smith_tidy_main(int argc, char **argv) {
    smith_tool_start("__smith_tidy", argv[0]);
    smith_affine_disable(); // SPIKE #189
    smith_effects_disable();
    // clang-tidy builds each file's command through the tooling library, whose last arguments adjuster
    // (getStripPluginsAdjuster) asks the hook: a .smith file gets the command `smith cc` gives it, whatever the
    // compile database says.
    clang::CommandHook = smith_command_hook;
#ifdef SMITH_HOOK
    smith_hook_install();
#endif
    smith_tidy_names_register(true);
    return clang::tidy::clangTidyMain(argc, const_cast<const char **>(argv));
}
