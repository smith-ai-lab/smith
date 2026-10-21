// A C interface to clang-tidy's own main, linked into smith as `smith lint`.
//
// clangTidyMain is the function clang-tidy's main calls (clang-tidy/tool/ClangTidyMain.cpp), so every flag, .clang-tidy
// file, check, fix and diagnostic is clang-tidy's own. See tool.h for how its initializers and its resource directory
// are handled.
#include "clang-tidy/tool/ClangTidyMain.h"
#include "clang/Tooling/ArgumentsAdjusters.h"
#include "tool.h"

#ifdef SMITH_HOOK
#include "clang-tidy/ClangTidyModule.h"
#include "clang-tidy/readability/IdentifierNamingCheck.h"
#include "names.h"
#endif

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"

#include <cstddef>
#include <vector>

extern "C" {
#include "../runtime/lang.h"
}

#ifdef SMITH_HOOK
// shim/hook.cpp: makes every translation unit with a .smith main file, clang-tidy's included, offer its tokens to the
// lowering and show its diagnostics (clang-tidy's own, fix-its included) with the names as written.
extern "C" void smith_hook_install();
#endif

// clang-tidy reads a file through clang's tooling library, which builds the compile command of each file by the driver
// (not through clang::createInvocation, as clangd does) and runs the arguments adjusters on it. The last adjuster
// clang-tidy appends, getStripPluginsAdjuster, is the one place to give a .smith file the command that `smith cc`
// gives it (smith_lang_command, the one rule of runtime/lang_command.c: `-x c`, and `-std=gnu2y` unless it names a
// standard), because a .smith extension is a linker input to the driver, whatever a project's compile database says.
// This is that file, clang/lib/Tooling/ArgumentsAdjusters.cpp (Apache-2.0 WITH LLVM-exception, the version of this
// LLVM), whole, with one addition in getStripPluginsAdjuster: smith defines every symbol the object has, so the linker
// takes these and leaves the archive's member out. A command with no .smith input is returned as it was.
namespace clang::tooling {

static StringRef getDriverMode(const CommandLineArguments &Args) {
    for (StringRef ArgRef : Args) {
        if (ArgRef.consume_front("--driver-mode=")) return ArgRef;
    }
    return StringRef();
}

/// Add -fsyntax-only option and drop options that triggers output generation.
ArgumentsAdjuster getClangSyntaxOnlyAdjuster() {
    return [](const CommandLineArguments &Args, StringRef /*unused*/) {
        CommandLineArguments AdjustedArgs;
        bool HasSyntaxOnly = false;
        constexpr llvm::StringRef OutputCommands[] = {
            "-save-temps",
            "--save-temps",
        };
        for (size_t i = 0, e = Args.size(); i < e; ++i) {
            StringRef Arg = Args[i];
            if (llvm::any_of(OutputCommands,
                             [&Arg](llvm::StringRef OutputCommand) { return Arg.starts_with(OutputCommand); }))
                continue;

            if (Arg != "-c" && Arg != "-S" && !Arg.starts_with("-fcolor-diagnostics") &&
                !Arg.starts_with("-fdiagnostics-color"))
                AdjustedArgs.push_back(Args[i]);
            else if (!AdjustedArgs.empty() && AdjustedArgs.back() == "-Xclang")
                AdjustedArgs.pop_back();

            if (Arg == "-fsyntax-only") HasSyntaxOnly = true;
        }
        if (!HasSyntaxOnly) AdjustedArgs = getInsertArgumentAdjuster("-fsyntax-only")(AdjustedArgs, "");
        return AdjustedArgs;
    };
}

ArgumentsAdjuster getClangStripOutputAdjuster() {
    return [](const CommandLineArguments &Args, StringRef /*unused*/) {
        CommandLineArguments AdjustedArgs;
        for (size_t i = 0, e = Args.size(); i < e; ++i) {
            StringRef Arg = Args[i];
            if (!Arg.starts_with("-o")) AdjustedArgs.push_back(Args[i]);

            if (Arg == "-o") ++i;
        }
        return AdjustedArgs;
    };
}

ArgumentsAdjuster getClangStripDependencyFileAdjuster() {
    return [](const CommandLineArguments &Args, StringRef /*unused*/) {
        auto UsingClDriver = (getDriverMode(Args) == "cl");

        CommandLineArguments AdjustedArgs;
        for (size_t i = 0, e = Args.size(); i < e; ++i) {
            StringRef Arg = Args[i];

            if (!UsingClDriver && (Arg == "-MF" || Arg == "-MT" || Arg == "-MQ")) {
                ++i;
                continue;
            }
            if (!UsingClDriver && Arg.starts_with("-M")) continue;
            if (Arg.starts_with("/showIncludes") || Arg.starts_with("-showIncludes")) continue;

            AdjustedArgs.push_back(Args[i]);
        }
        return AdjustedArgs;
    };
}

ArgumentsAdjuster getInsertArgumentAdjuster(const CommandLineArguments &Extra, ArgumentInsertPosition Pos) {
    return [Extra, Pos](const CommandLineArguments &Args, StringRef /*unused*/) {
        CommandLineArguments Return(Args);

        CommandLineArguments::iterator I;
        if (Pos == ArgumentInsertPosition::END) {
            I = llvm::find(Return, "--");
        } else {
            I = Return.begin();
            ++I; // To leave the program name in place
        }

        Return.insert(I, Extra.begin(), Extra.end());
        return Return;
    };
}

ArgumentsAdjuster getInsertArgumentAdjuster(const char *Extra, ArgumentInsertPosition Pos) {
    return getInsertArgumentAdjuster(CommandLineArguments(1, Extra), Pos);
}

ArgumentsAdjuster combineAdjusters(ArgumentsAdjuster First, ArgumentsAdjuster Second) {
    if (!First) return Second;
    if (!Second) return First;
    return
        [First, Second](const CommandLineArguments &Args, StringRef File) { return Second(First(Args, File), File); };
}

// The command as `smith cc` makes it for its .smith inputs; the arguments as they were for any other.
static CommandLineArguments smithLangArguments(const CommandLineArguments &Args) {
    std::vector<char *> in;
    for (const std::string &arg : Args)
        in.push_back(const_cast<char *>(arg.c_str()));
    std::vector<char *> out(in.size() + 16);
    int count = smith_lang_command((int)in.size(), in.data(), 1, out.data(), out.size() - 1);
    if (count <= 0) return Args;
    return CommandLineArguments(out.begin(), out.begin() + count);
}

ArgumentsAdjuster getStripPluginsAdjuster() {
    return [](const CommandLineArguments &Args, StringRef /*unused*/) {
        CommandLineArguments AdjustedArgs;
        for (size_t I = 0, E = Args.size(); I != E; I++) {
            // According to https://clang.llvm.org/docs/ClangPlugins.html
            // plugin arguments are in the form:
            // -Xclang {-load, -plugin, -plugin-arg-<plugin-name>, -add-plugin}
            // -Xclang <arbitrary-argument>
            if (I + 4 < E && Args[I] == "-Xclang" &&
                (Args[I + 1] == "-load" || Args[I + 1] == "-plugin" ||
                 llvm::StringRef(Args[I + 1]).starts_with("-plugin-arg-") || Args[I + 1] == "-add-plugin") &&
                Args[I + 2] == "-Xclang") {
                I += 3;
                continue;
            }
            AdjustedArgs.push_back(Args[I]);
        }
        return smithLangArguments(AdjustedArgs);
    };
}

} // namespace clang::tooling

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
        const smith::WrittenName *written = writtenName(Decl, SM);
        if (written == nullptr || written->member.empty()) return (inner.*reach(DeclFn{}))(Decl, SM);
        // The first declaration of an exported function is the lowering's copy of its prototype, which is in no file
        // the user's rules apply to; the definition is the text the user wrote, so it is the one judged. (The finding
        // is made at the copy's place, which smith::presentThrough's consumer moves to the token the copy was made
        // from.)
        const NamedDecl *judged = definitionOf(Decl);
        auto *mutableDecl = const_cast<NamedDecl *>(judged);
        const DeclarationName lowered = mutableDecl->getDeclName();
        mutableDecl->setDeclName(DeclarationName(&Decl->getASTContext().Idents.get(written->member)));
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
            if (written != nullptr && !written->member.empty()) name = written->member;
        }
        return DiagInfo{"invalid case style for %0 '%1'",
                        [kind = Failure.Info.KindName, name](DiagnosticBuilder &Diag) { Diag << kind << name; }};
    }

  private:
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
void attach(ClangTidyContext *Context) {
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
        Factories.registerCheckFactory("readability-identifier-naming", [](StringRef Name, ClangTidyContext *Context) {
            attach(Context);
            return std::make_unique<SmithNaming>(Name, Context);
        });
    }
};

} // namespace
} // namespace clang::tidy
#endif

// Runs clang-tidy on argv[1 .. argc), as argv[0], <folder>/bin/clang-tidy, in clang's folder. Returns
// its exit status.
extern "C" int smith_tidy_main(int argc, char **argv) {
    smith_tool_start("__smith_tidy", argv[0]);
#ifdef SMITH_HOOK
    smith_hook_install();
    // After the modules clang-tidy registers (which are in by now), so that it wraps their checks.
    static const llvm::Registry<clang::tidy::ClangTidyModule>::Add<clang::tidy::SmithModule> smith_module(
        "smith-lang", "checks see the names a .smith file wrote");
#endif
    return clang::tidy::clangTidyMain(argc, const_cast<const char **>(argv));
}
