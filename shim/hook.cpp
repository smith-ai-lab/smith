// The clang side of .smith files: the hook that gives the lowering (runtime/lang.c) the tokens of a .smith file, after
// macro expansion and before the parser, puts back what it emits, and answers its questions from the parser's tables.
//
// The hook itself is a patch to clang (patches/clang-token-hook.patch): clang::TokenHook, offered each token that
// leaves Preprocessor::Lex. Everything here is smith's. It stays small and C++ only because clang's API is: the logic
// is in C, in the lowering.
#include "names.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/Expr.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticParse.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/CodeGen/ModuleBuilder.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Sema/ExternalSemaSource.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/Sema.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/VirtualFileSystem.h"

extern "C" {
#include "../runtime/lang.h"
}

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <unistd.h>

extern char **environ;

using namespace clang;

namespace {

// The first error a parse reports, kept instead of printed and left out of the counts.
class DiagnosticCollector : public DiagnosticConsumer {
  public:
    std::string first;
    bool IncludeInDiagnosticCounts() const override {
        return false;
    }
    void HandleDiagnostic(DiagnosticsEngine::Level level, const Diagnostic &info) override {
        if (level < DiagnosticsEngine::Error || !first.empty()) return;
        SmallString<256> message;
        info.FormatDiagnostic(message);
        first = message.str().str();
    }
};

// One thing to put back in front of the parser, in order: tokens, or a header to include.
struct Action {
    bool include = false;
    bool angled = false;
    std::vector<Token> tokens;
    std::vector<bool> reinjected;   // per token: put back as a copy the parser's tokens do not count (see replay)
    std::vector<bool> synthetic;    // per token: the source has no token of it (see relocate)
    std::vector<uint32_t> instance; // per token: the generic instance whose body it is in (0: none)
    std::vector<std::string> text;  // per token of an instance: its spelling
    std::string header;
    std::string preface; // lines to include first (the headers an interface read in memory needs)
    std::string body;    // the text of a file made in memory, entered as a system file (issue #107)
    bool literal = false;
    SourceLocation at;
};

// The lowering (runtime/lang*.c) is plain C with static tables, no malloc, so a process has one of it. A compile has
// one translation unit and never notices; clangd parses many at once (a thread for each open file, the background
// index), so a hook takes the lowering when it is made and gives it back when it has seen the end of its file (or when
// its parse is torn down). Parses of .smith files in one process are one at a time between those two points; headers
// and C files are not held up. It is not a mutex because a hook may be destroyed on another thread than the one that
// made it.
struct Lowering {
    static inline std::mutex lock;
    static inline std::condition_variable freed;
    static inline bool busy = false;

    static void acquire(bool &holding) {
        std::unique_lock<std::mutex> guard(lock);
        freed.wait(guard, [] { return !busy; });
        busy = true;
        holding = true;
    }

    static void release(bool &holding) {
        if (!holding) return;
        {
            std::lock_guard<std::mutex> guard(lock);
            busy = false;
        }
        holding = false;
        freed.notify_one();
    }
};

// Where the Presenter asks what a name is written as: the hook that made the diagnostic.
class NameSource {
  public:
    virtual ~NameSource() = default;
    virtual bool writtenName(StringRef lowered, smith::WrittenName &out) = 0;
    // Where a token at a place that holds a copy was copied from (invalid for any other place).
    virtual SourceLocation originOf(SourceLocation at) = 0;
    // Where in a module's source the declaration that stands at a place of its generated header is (invalid for any
    // other place).
    virtual SourceLocation sourceOf(SourceLocation at) = 0;
    // When a place is in the code of a generic instance: the notes that say where the instance was asked for, the
    // innermost first (each a place and its text).
    virtual bool instanceNotes(SourceLocation at, std::vector<std::pair<SourceLocation, std::string>> &notes) = 0;
    // Whether a place is in an interface the lowering generated (a module's header, or its source read in memory as
    // one) and not in text the user wrote.
    virtual bool generatedAt(SourceLocation at) = 0;
};

// What a hook shares with the consumer it puts in front of the diagnostics: the hook may be gone when a late diagnostic
// is made, so it takes its name from here.
struct PresenterLink {
    NameSource *source = nullptr;
    unsigned noteId = 0; // a custom note that shows its text
    std::set<unsigned> suppressed;
    std::map<unsigned, std::string> hints; // by place: a note for the error the compiler reports there
};

// The engine a tool reports its own findings from, which the next hook of this thread puts its Presenter in front of
// (see smith::presentThrough), and the Presenter it made.
bool inEditor = false;
thread_local DiagnosticsEngine *toolEngine = nullptr;
thread_local DiagnosticConsumer *toolConsumer = nullptr;

// Shows the diagnostics of a .smith file as the source wrote it, in front of whatever shows them (clang's printer, a
// language server):
//   - the names are the written ones, `foo::f` for the compiler's `foo_f`, in every message, note and fix-it. This is
//     a rewrite of the formatted text by the lowering's table of names (smith_lang_written_find), not a guess at the
//     underscores, and it reaches what clang prints for any kind of argument;
//   - the uses of a module that could not be imported are kept out, since the import has reported it. A note that
//     belongs to one that is kept out goes too.
class Presenter final : public DiagnosticConsumer {
  public:
    Presenter(std::unique_ptr<DiagnosticConsumer> owned, DiagnosticConsumer *inner, std::shared_ptr<PresenterLink> link)
        : owned(std::move(owned)), inner(inner), link(std::move(link)) {
        NumWarnings = inner->getNumWarnings();
        NumErrors = inner->getNumErrors();
    }
    void BeginSourceFile(const LangOptions &options, const Preprocessor *pp) override {
        inner->BeginSourceFile(options, pp);
    }
    void EndSourceFile() override {
        flushInstanceNotes();
        inner->EndSourceFile();
    }
    void clear() override {
        DiagnosticConsumer::clear();
        flushInstanceNotes();
        inner->clear();
    }
    bool IncludeInDiagnosticCounts() const override {
        return inner->IncludeInDiagnosticCounts();
    }
    void HandleDiagnostic(DiagnosticsEngine::Level level, const Diagnostic &info) override {
        if (level != DiagnosticsEngine::Note) flushInstanceNotes(); // the notes of the last error come after its own
        if (level == DiagnosticsEngine::Note) {
            if (dropNotes) return;
        } else {
            dropNotes =
                info.getLocation().isValid() && link->suppressed.count(info.getLocation().getRawEncoding()) != 0;
            if (dropNotes) return;
        }
        // A diagnostic in a copy (a prototype's) is the definition's again, which has its own; a note that points into
        // one points at the token it copies.
        SourceLocation at = info.getLocation();
        if (link->source != nullptr) {
            // A tool's finding about the interface of an imported module is about text the user did not write there:
            // the module's own file is checked as itself.
            if (tool && level != DiagnosticsEngine::Note && link->source->generatedAt(at)) {
                dropNotes = true;
                return;
            }
            SourceLocation origin = link->source->originOf(at);
            std::vector<std::pair<SourceLocation, std::string>> chain;
            bool instance = origin.isValid() && link->source->instanceNotes(at, chain);
            // A tool reports a name once, at the first declaration it finds, which is the copy: the finding is the
            // definition's (and the compiler's, which has its own, is not).
            if (origin.isValid() && level != DiagnosticsEngine::Note && !tool && !instance) {
                dropNotes = true;
                return;
            }
            if (instance &&
                level != DiagnosticsEngine::Note) { // an error in an instance: where it was asked for follows
                pendingNotes = std::move(chain);
                engine = info.getDiags();
            }
            auto hint = level == DiagnosticsEngine::Error && at.isValid() ? link->hints.find(at.getRawEncoding())
                                                                          : link->hints.end();
            if (hint != link->hints.end()) { // the lowering knows why the name is not found
                pendingNotes.insert(pendingNotes.begin(), {at, hint->second});
                engine = info.getDiags();
            }
            if (origin.isValid()) at = origin;
            // A place in a module's generated header is where the module's source declares it.
            SourceLocation declared = link->source->sourceOf(at);
            if (declared.isValid()) at = declared;
        }
        DiagnosticConsumer::HandleDiagnostic(level, info);
        if (link->source != nullptr && !tool && failureReport(level, info, at)) return;
        if (link->source == nullptr || !present(level, info, at)) inner->HandleDiagnostic(level, info);
    }

  private:
    std::unique_ptr<DiagnosticConsumer> owned;
    DiagnosticConsumer *inner;
    std::shared_ptr<PresenterLink> link;
    bool dropNotes = false;
    std::vector<std::pair<SourceLocation, std::string>> pendingNotes; // of the error just passed on
    const DiagnosticsEngine *engine = nullptr;

    // ---- A failed contract or check at compile time, in the form of the run-time failure report
    //
    // The evaluator stops at a call of a function annotated smith_compile_error or smith_compile_check (the contracts'
    // and the checks' failure paths) with one note per call, innermost first: the failing function, the inline
    // wrappers a contract goes through (`smith_requires`), then the compile-time calls that led there. The run-time
    // report names the failure and skips that machinery (<smith/trace.h>), so this does too: the machinery's notes
    // become one, "<heading> in call to '<the contract as written>'", at the line of the contract in the user's code,
    // and the notes after it are the stack, innermost first, with their file, line and arguments.
    struct HeldFailure {
        bool active = false;
        std::string heading; // "requires failed"
        std::string call;    // the machinery's outermost call, as the source writes it
        SourceLocation where;
        const DiagnosticsEngine *engine = nullptr;
    } held;

    static std::string failureHeading(StringRef call) {
        if (call.starts_with("smith_contract_requires(")) return "requires failed";
        if (call.starts_with("smith_contract_ensures(")) return "ensures failed";
        if (call.starts_with("smith_contract_invariant(")) return "invariant failed";
        if (call.starts_with("smith_contract_assert(")) return "assert failed";
        if (call.starts_with("smith_test_check_condition(")) return "check failed";
        if (call.starts_with("smith_failure(")) {
            static const std::pair<StringRef, StringRef> kinds[] = {
                {"SmithFailure_Requires", "requires failed"},       {"SmithFailure_Ensures", "ensures failed"},
                {"SmithFailure_Invariant", "invariant failed"},     {"SmithFailure_Assert", "assert failed"},
                {"SmithFailure_Capacity", "arena out of capacity"}, {"SmithFailure_Check", "check failed"}};
            for (auto &[kind, heading] : kinds)
                if (call.contains(kind)) return std::string(heading);
            return "failed";
        }
        return "";
    }

    // The functions the run-time report skips when it chooses the failing frame (smith_failure in <smith/trace.h>).
    static bool isMachinery(StringRef call) {
        static const StringRef prefixes[] = {"smith_failure",  "smith_contract_", "smith_check",     "smith_test_check",
                                             "smith_requires", "smith_ensures",   "smith_invariant", "smith_assert"};
        return llvm::any_of(prefixes, [&](StringRef prefix) { return call.starts_with(prefix); });
    }

    void emitHeld(SourceLocation where) {
        if (!held.active) return;
        DiagnosticStorage storage;
        std::string text = held.heading + " in call to '" + written(held.call) + "'";
        Diagnostic note(held.engine, where, link->noteId, storage, text);
        held.active = false;
        inner->HandleDiagnostic(DiagnosticsEngine::Note, note);
    }

    // True when the diagnostic is a machinery note that became the failure's.
    bool failureReport(DiagnosticsEngine::Level level, const Diagnostic &info, SourceLocation at) {
        if (level != DiagnosticsEngine::Note) {
            emitHeld(held.where);
            return false;
        }
        SmallString<256> formatted;
        info.FormatDiagnostic(formatted);
        StringRef text = formatted;
        bool call = text.consume_front("in call to '") && text.consume_back("'");
        if (call && isMachinery(text)) {
            std::string heading = failureHeading(text);
            if (!held.active && !heading.empty()) {
                held.active = true;
                held.heading = heading;
                held.engine = info.getDiags();
            }
            if (!held.active) return false;
            held.where = at; // a note is where its function was called: the last is the contract's line in user code
            held.call = text.str(); // the last is the outermost wrapper, which the source calls
            return true;
        }
        emitHeld(held.where);
        return false;
    }

    // The notes that name the instances an error is in, after the error and its own notes.
    void flushInstanceNotes() {
        emitHeld(held.where);
        std::vector<std::pair<SourceLocation, std::string>> notes = std::move(pendingNotes);
        pendingNotes.clear();
        for (auto &[where, text] : notes) {
            DiagnosticStorage storage;
            Diagnostic note(engine, where, link->noteId, storage, text);
            inner->HandleDiagnostic(DiagnosticsEngine::Note, note);
        }
    }
    bool tool = false; // in front of the engine of a tool's findings, not the compiler's

  public:
    // The next translation unit's hook speaks through the same consumer (see smith::presentThrough).
    void relink(std::shared_ptr<PresenterLink> next) {
        link = std::move(next);
        dropNotes = false;
    }

    // Puts a consumer like this one in front of the engine a tool reports its own findings from, or hands the one
    // already there to `link`.
    static void presentToolEngine(const std::shared_ptr<PresenterLink> &link);

  private:
    // The text with the names written as the source writes them.
    std::string written(StringRef text) {
        std::string out;
        out.reserve(text.size());
        for (size_t at = 0; at < text.size();) {
            if (!isAsciiIdentifierStart(text[at])) {
                out += text[at++];
                continue;
            }
            size_t end = at + 1;
            while (end < text.size() && isAsciiIdentifierContinue(text[end]))
                end += 1;
            smith::WrittenName name;
            StringRef word = text.substr(at, end - at);
            if (word.contains('_') && link->source->writtenName(word, name)) {
                out += name.written;
            } else {
                out += word;
            }
            at = end;
        }
        return out;
    }

    // The text a fix-it puts in: a name behind `x::` is the member alone, any other the way the source writes it.
    std::string fixed(const Diagnostic &info, const FixItHint &hint) {
        smith::WrittenName name;
        if (hint.CodeToInsert.empty() || !link->source->writtenName(hint.CodeToInsert, name)) return hint.CodeToInsert;
        return behindColons(info, hint.RemoveRange.getBegin()) ? name.member : name.written;
    }

    static bool behindColons(const Diagnostic &info, SourceLocation at) {
        if (!info.hasSourceManager() || !at.isFileID()) return false;
        SourceManager &sm = info.getSourceManager();
        auto [file, offset] = sm.getDecomposedLoc(at);
        bool invalid = false;
        StringRef text = sm.getBufferData(file, &invalid);
        if (invalid || offset > text.size()) return false;
        while (offset > 0 && (text[offset - 1] == ' ' || text[offset - 1] == '\t'))
            offset -= 1;
        return offset >= 2 && text[offset - 1] == ':' && text[offset - 2] == ':';
    }

    // Passes the diagnostic on with its names written; false when it has nothing to change.
    bool present(DiagnosticsEngine::Level level, const Diagnostic &info, SourceLocation at) {
        SmallString<256> formatted;
        info.FormatDiagnostic(formatted);
        std::string shown = written(formatted);
        bool changed = shown != formatted.str() || at != info.getLocation();
        std::vector<FixItHint> hints(info.getFixItHints().begin(), info.getFixItHints().end());
        if (tool) { // a fix in a copy or in a generated interface is not a change to the user's text
            size_t before = hints.size();
            llvm::erase_if(hints, [this](const FixItHint &hint) {
                SourceLocation begin = hint.RemoveRange.getBegin();
                return link->source->originOf(begin).isValid() || link->source->generatedAt(begin);
            });
            changed = changed || hints.size() != before;
        }
        for (FixItHint &hint : hints) {
            std::string code = fixed(info, hint);
            if (code == hint.CodeToInsert) continue;
            hint.CodeToInsert = code;
            changed = true;
        }
        if (!changed) return false;
        DiagnosticStorage storage;
        storage.NumDiagArgs = info.getNumArgs();
        for (unsigned i = 0; i < info.getNumArgs(); i++) {
            storage.DiagArgumentsKind[i] = info.getArgKind(i);
            if (info.getArgKind(i) == DiagnosticsEngine::ak_std_string) {
                storage.DiagArgumentsStr[i] = info.getArgStdStr(i);
            } else {
                storage.DiagArgumentsVal[i] = info.getRawArg(i);
            }
        }
        if (at == info.getLocation()) storage.DiagRanges.assign(info.getRanges().begin(), info.getRanges().end());
        storage.FixItHints.assign(hints.begin(), hints.end());
        Diagnostic copy(info.getDiags(), at, info.getID(), storage, shown);
        inner->HandleDiagnostic(level, copy);
        return true;
    }
};

// ---- Arrays keep their shape in a module (issue #81)
//
// A parameter `T xs[N]` of a module's function is N objects, not a pointer: clang adjusts its type to a pointer but
// keeps the written type (ParmVarDecl::getOriginalType), which is what this reads. Each use that would treat it as a
// pointer, or break its size, is an error: a constant index out of range, `xs++` and `xs = p`, `sizeof(xs)` (the
// pointer's size), and a call that passes a pointer or an array of another shape. Nothing here changes what valid C
// means: it only rejects. It runs when the translation unit is complete, over the functions the module's source
// defines. (A parameter with no size is rejected by the lowering, at its declaration.)
class ArrayParameters {
  public:
    ArrayParameters(ASTContext &context, SourceManager &manager, std::function<bool(SourceLocation)> generated)
        : context(context), diagnostics(context.getDiagnostics()), manager(manager), generated(std::move(generated)) {
        auto error = DiagnosticsEngine::Error;
        outOfRange = diagnostics.getCustomDiagID(error, "index %0 is out of range for the array parameter '%1' of %2 "
                                                        "elements");
        modify = diagnostics.getCustomDiagID(error, "the array parameter '%0' is not a pointer: it cannot be changed "
                                                    "(write `%1 *%0` for a pointer)");
        sizeofArray = diagnostics.getCustomDiagID(error, "sizeof of the array parameter '%0' is the size of a pointer "
                                                         "here: write countof(%0) for its elements");
        pointerArgument = diagnostics.getCustomDiagID(error, "the parameter '%0' is an array of %1 elements: pass an "
                                                             "array of that shape, not a pointer");
        shapeArgument = diagnostics.getCustomDiagID(error, "the parameter '%0' is an array of %1 elements: the "
                                                           "argument is an array of another shape");
    }

    void run() {
        for (Decl *decl : context.getTranslationUnitDecl()->decls()) {
            auto *function = dyn_cast<FunctionDecl>(decl);
            if (function == nullptr || !function->doesThisDeclarationHaveABody()) continue;
            if (!manager.isWrittenInMainFile(manager.getSpellingLoc(function->getLocation()))) continue;
            std::vector<const Stmt *> pending{function->getBody()}; // a worklist, not a recursion
            while (!pending.empty()) {
                const Stmt *statement = pending.back();
                pending.pop_back();
                if (statement == nullptr) continue;
                check(statement);
                for (const Stmt *child : statement->children())
                    pending.push_back(child);
            }
        }
    }

  private:
    void check(const Stmt *statement) {
        if (auto *subscript = dyn_cast<ArraySubscriptExpr>(statement)) checkSubscript(subscript);
        if (auto *op = dyn_cast<UnaryOperator>(statement)) checkIncrement(op);
        if (auto *op = dyn_cast<BinaryOperator>(statement)) checkAssignment(op);
        if (auto *trait = dyn_cast<UnaryExprOrTypeTraitExpr>(statement)) checkSizeof(trait);
        if (auto *call = dyn_cast<CallExpr>(statement)) checkCall(call);
    }

    void checkSubscript(const ArraySubscriptExpr *subscript) {
        const Expr *base = subscript->getBase()->IgnoreParenImpCasts();
        const ParmVarDecl *parameter = nullptr;
        uint64_t size = 0;
        if (auto *array = parameterArray(base, &parameter)) {
            size = array->getZExtSize();
        } else if (auto *inner = dyn_cast<ConstantArrayType>(base->getType().getTypePtr())) {
            parameter = isa<ArraySubscriptExpr>(base) ? rootParameter(base) : nullptr;
            size = inner->getZExtSize();
        }
        if (parameter == nullptr) return;
        Expr::EvalResult result;
        if (!subscript->getIdx()->EvaluateAsInt(result, context)) return;
        const llvm::APSInt &index = result.Val.getInt();
        bool below = index.isSigned() && index.isNegative();
        if (below || index.getZExtValue() >= size) {
            std::string shown = below ? std::to_string(index.getSExtValue()) : std::to_string(index.getZExtValue());
            report(subscript->getIdx()->getBeginLoc(), outOfRange, shown, parameter, size);
        }
    }

    void checkIncrement(const UnaryOperator *op) {
        if (!op->isIncrementDecrementOp()) return;
        const ParmVarDecl *parameter = nullptr;
        if (parameterArray(op->getSubExpr()->IgnoreParens(), &parameter)) reportModify(op->getOperatorLoc(), parameter);
    }

    void checkAssignment(const BinaryOperator *op) {
        if (!op->isAssignmentOp()) return;
        const ParmVarDecl *parameter = nullptr;
        if (parameterArray(op->getLHS()->IgnoreParens(), &parameter)) reportModify(op->getOperatorLoc(), parameter);
    }

    void checkSizeof(const UnaryExprOrTypeTraitExpr *trait) {
        if (trait->getKind() != UETT_SizeOf || trait->isArgumentType()) return;
        const ParmVarDecl *parameter = nullptr;
        if (parameterArray(trait->getArgumentExpr()->IgnoreParens(), &parameter)) {
            diagnostics.Report(trait->getOperatorLoc(), sizeofArray) << parameter->getName();
        }
    }

    void checkCall(const CallExpr *call) {
        const FunctionDecl *callee = call->getDirectCallee();
        if (callee == nullptr || !fromModule(callee)) return;
        for (unsigned i = 0; i < callee->getNumParams() && i < call->getNumArgs(); i++) {
            const ParmVarDecl *parameter = callee->getParamDecl(i);
            const auto *wanted = dyn_cast<ConstantArrayType>(parameter->getOriginalType().getTypePtr());
            if (wanted == nullptr) continue;
            const Expr *argument = call->getArg(i);
            QualType given = shapeOf(argument->IgnoreParens());
            std::string count = std::to_string(wanted->getZExtSize());
            if (given.isNull()) {
                diagnostics.Report(argument->getBeginLoc(), pointerArgument) << parameter->getName() << count;
            } else if (!sameShape(parameter->getOriginalType(), given)) {
                diagnostics.Report(argument->getBeginLoc(), shapeArgument) << parameter->getName() << count;
            }
        }
    }

    ASTContext &context;
    DiagnosticsEngine &diagnostics;
    SourceManager &manager;
    std::function<bool(SourceLocation)> generated;
    unsigned outOfRange, modify, sizeofArray, pointerArgument, shapeArgument;

    // Whether the function is a module's (declared in the file or a header the lowering made), not C's: C's takes a
    // pointer, as in C.
    bool fromModule(const FunctionDecl *function) const {
        SourceLocation at = function->getLocation();
        return manager.isWrittenInMainFile(manager.getSpellingLoc(at)) || generated(at);
    }

    // The array an expression is, when it names a parameter written as an array.
    const ConstantArrayType *parameterArray(const Expr *expression, const ParmVarDecl **parameter) const {
        auto *reference = dyn_cast<DeclRefExpr>(expression);
        if (reference == nullptr) return nullptr;
        auto *declaration = dyn_cast<ParmVarDecl>(reference->getDecl());
        if (declaration == nullptr) return nullptr;
        auto *array = dyn_cast<ConstantArrayType>(declaration->getOriginalType().getTypePtr());
        if (array != nullptr) *parameter = declaration;
        return array;
    }

    // The array parameter at the root of `xs[i][j]`, or null.
    const ParmVarDecl *rootParameter(const Expr *expression) const {
        while (auto *subscript = dyn_cast<ArraySubscriptExpr>(expression->IgnoreParenImpCasts()))
            expression = subscript->getBase();
        const ParmVarDecl *parameter = nullptr;
        return parameterArray(expression->IgnoreParenImpCasts(), &parameter) ? parameter : nullptr;
    }

    // The array an argument is (before it decays), or null when it is a pointer.
    QualType shapeOf(const Expr *argument) const {
        auto *cast = dyn_cast<ImplicitCastExpr>(argument);
        while (cast != nullptr && cast->getCastKind() == CK_NoOp) // `const` added
            cast = dyn_cast<ImplicitCastExpr>(cast->getSubExpr()->IgnoreParens());
        if (cast == nullptr) return QualType();
        const Expr *inner = cast->getSubExpr()->IgnoreParens();
        if (cast->getCastKind() == CK_ArrayToPointerDecay) return inner->getType();
        if (cast->getCastKind() == CK_LValueToRValue) { // an array parameter passed on
            const ParmVarDecl *parameter = nullptr;
            if (parameterArray(inner, &parameter)) return parameter->getOriginalType();
        }
        return QualType();
    }

    // Whether two array types have the same dimensions and the same elements, apart from `const`.
    bool sameShape(QualType wanted, QualType given) const {
        while (true) {
            auto *a = dyn_cast<ConstantArrayType>(context.getCanonicalType(wanted).getTypePtr());
            auto *b = dyn_cast<ConstantArrayType>(context.getCanonicalType(given).getTypePtr());
            if (a == nullptr || b == nullptr) break;
            if (a->getSize() != b->getSize()) return false;
            wanted = a->getElementType();
            given = b->getElementType();
        }
        return context.hasSameUnqualifiedType(wanted, given);
    }

    void report(SourceLocation at, unsigned id, const std::string &index, const ParmVarDecl *parameter, uint64_t size) {
        diagnostics.Report(at, id) << index << parameter->getName() << std::to_string(size);
    }

    void reportModify(SourceLocation at, const ParmVarDecl *parameter) {
        QualType element = cast<ConstantArrayType>(parameter->getOriginalType().getTypePtr())->getElementType();
        diagnostics.Report(at, modify) << parameter->getName() << element.getAsString();
    }
};

class SmithHook;

class ArrayEnd final : public ExternalSemaSource {
  public:
    explicit ArrayEnd(SmithHook &hook) : hook(&hook) {}
    void ReadTentativeDefinitions(SmallVectorImpl<VarDecl *> &) override;
    SmithHook *hook;
};

class SmithHook final : public TokenHook, public NameSource {
  public:
    bool holding = false; // this hook has the lowering
    explicit SmithHook(Preprocessor &pp) : PP(pp), SM(pp.getSourceManager()), MainFile(SM.getMainFileID()) {
        mainBegin = SM.getLocForStartOfFile(MainFile).getRawEncoding();
        mainEnd = SM.getLocForEndOfFile(MainFile).getRawEncoding();
        Lowering::acquire(holding);
        lang = smith_lang_new();
        host.context = this;
        host.next = &SmithHook::nextToken;
        host.emit = &SmithHook::emitToken;
        host.include = &SmithHook::includeHeader;
        host.error = &SmithHook::reportError;
        host.watch = &SmithHook::watchName;
        host.watch_mode = &SmithHook::watchMode;
        host.previous = &SmithHook::previousToken;
        host.declared = &SmithHook::declaredInFile;
        host.watch_imports = &SmithHook::watchImports;
        host.rename = &SmithHook::renameName;
        host.lookup = &SmithHook::lookupName;
        host.declared_by = &SmithHook::declaredBy;
        host.type_info = &SmithHook::typeInfo;
        host.type_query = &SmithHook::typeQuery;
        host.return_type = &SmithHook::returnType;
        host.expr_type = &SmithHook::exprType;
        host.value_query = &SmithHook::valueQuery;
        host.instance = &SmithHook::instanceBegin;
        host.locate = &SmithHook::locatePlace;
        host.types_place = &SmithHook::typesPlace;
        host.mentions = &SmithHook::mentionsName;
        host.read_body = &SmithHook::readBody;
        host.include_text = &SmithHook::includeText;
        host.field = &SmithHook::fieldOf;
        host.type_field = &SmithHook::typeField;
        host.read_header = &SmithHook::readHeader;
        host.read_problem = &SmithHook::readProblem;
        host.suppress = &SmithHook::suppressUse;
        host.hint = &SmithHook::hintAt;
        if (OptionalFileEntryRef entry = SM.getFileEntryRefForID(MainFile)) {
            mainName = entry->getName().str();
        }
        host.main_file = mainName.c_str();
        host.maybe_main = SM.getBufferData(MainFile).contains("main");
        host.maybe_generic = SM.getBufferData(MainFile).contains("type ") ||
                             SM.getBufferData(MainFile).contains("auto ") ||
                             SM.getBufferData(MainFile).contains("auto...");
        host.maybe_if_constexpr = hasIfConstexpr(SM.getBufferData(MainFile));
        host.maybe_slice = hasSlice(SM.getBufferData(MainFile)) || hasStr(SM.getBufferData(MainFile));
        host.maybe_tuple =
            host.maybe_slice || hasTuple(SM.getBufferData(MainFile)) || hasPayloadEnum(SM.getBufferData(MainFile));
        host.maybe_field = hasFieldsUse(SM.getBufferData(MainFile));
        host.import_lines = countImportLines(SM.getBufferData(MainFile));
        host.type_values = &SmithHook::typeValues;
        host.maybe_type_value = host.maybe_field || hasTypeValue(SM.getBufferData(MainFile));
        host.target_fact = &SmithHook::targetFact;
        host.constant = &SmithHook::constantValue;
        // A tool that reports from an engine of its own (clang-tidy) numbers its custom diagnostics apart from the
        // compiler's, and would take this one's number for one of its checks'; a built-in diagnostic that is the text
        // alone has no such number.
        diagnostic = toolEngine != nullptr ? diag::err_apinotes_message
                                           : PP.getDiagnostics().getCustomDiagID(DiagnosticsEngine::Error, "%0");
        link->noteId = toolEngine != nullptr ? diag::err_apinotes_message
                                             : PP.getDiagnostics().getCustomDiagID(DiagnosticsEngine::Note, "%0");
        // clangd's preamble of a file that starts with code, as every module does, is the empty text before it, and
        // under -Wpedantic that is "a translation unit with no declaration". The file is not empty.
        if (SM.getBufferData(MainFile).empty()) {
            PP.getDiagnostics().setSeverity(diag::ext_empty_translation_unit, diag::Severity::Ignored,
                                            SourceLocation());
        }
        stats.on = getenv("SMITH_HOOK_STATS") != nullptr;
        debugOwner = this;
        link->source = this;
        installPresenter();
        smith_lang_watch_basics(&host);
        PP.addPPCallbacks(std::make_unique<Macros>(*this));
        for (unsigned i = 0; i < 13; i++) {
            sizedInfos[i] = PP.getIdentifierInfo(sizedTable[i].name);
            sizedNames[sizedInfos[i]] = i;
        }
    }

    void semaCreated(Sema &s) override {
        sema = &s;
        if (!arrayEnd) {
            // Sema asks an external source for the tentative definitions when the translation unit is complete, which
            // is when every function is whole: the end of the file, for the checks of a module's array parameters.
            arrayEnd = new ArrayEnd(*this);
            s.addExternalSource(arrayEnd);
        }
    }
    void semaDestroyed() override {
        sema = nullptr;
        giveBack(); // a parse that stopped before the end of the file
    }

    // SMITH_HOOK_STATS=1: what the hook was offered and what it cost, printed at the end of the file.
    struct Stats {
        bool on = false;
        uint64_t offered = 0, outside = 0, kept = 0, in_place = 0, streams = 0, held = 0, fast = 0;
        double nanoseconds = 0, slow_nanoseconds = 0;
    } stats;

    void report() {
        if (!stats.on) return;
        fprintf(stderr,
                "hook: offered %llu (outside the file %llu), kept %llu, fast %llu, in place %llu, streams %llu, held "
                "%llu; %.2f ms (slow path %.2f)\n",
                (unsigned long long)stats.offered, (unsigned long long)stats.outside, (unsigned long long)stats.kept,
                (unsigned long long)stats.fast, (unsigned long long)stats.in_place, (unsigned long long)stats.streams,
                (unsigned long long)stats.held, stats.nanoseconds / 1e6, stats.slow_nanoseconds / 1e6);
    }

    // The hook of this thread's compile, which names the debug info (debugName below).
    static inline thread_local NameSource *debugOwner = nullptr;

    ~SmithHook() override {
        if (debugOwner == this) debugOwner = nullptr;
        link->source = nullptr;
        giveBack();
    }

    bool transform(Preprocessor &, Token &tok) override {
        if (replayed(tok)) return false;
        bool end = tok.is(tok::eof); // the lowering is done with its state once it has seen the end of the file
        if (end) reachedEnd = true;
        if (end) moduleFile = smith_lang_is_module(lang);
        bool consumed = measured(tok);
        if (end) giveBack();
        return consumed;
    }

    bool measured(Token &tok) {
        if (!stats.on) return offer(tok);
        if (tok.is(tok::eof)) report();
        auto begin = std::chrono::steady_clock::now();
        bool consumed = offer(tok);
        stats.nanoseconds += std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - begin).count();
        stats.offered += 1;
        return consumed;
    }

    // A sized type name starts a short look at what follows it: a C keyword there is the program's name for something
    // (`i32 double(i32 x)`), and is reported as that, once, at the name. The hook is offered everything for as long as
    // the look lasts, and what it would not have been offered is passed over.
    bool offer(Token &tok) {
        if (!spelledInMain(tok.getLocation()) || tok.is(tok::eof)) {
            if (afterSized > 0) endLook();
            return offerLowered(tok);
        }
        if (afterSized > 0 && !look(tok)) return false;
        // A sized type name, or a typedef of the module: a keyword after it is a name (`Box double`).
        bool sized = tok.is(tok::identifier) && (sizedNames.find(tok.getIdentifierInfo()) != sizedNames.end() ||
                                                 smith_lang_type_name(lang, tok.getIdentifierInfo()->getNameStart(),
                                                                      tok.getIdentifierInfo()->getLength()));
        if (sized) {
            const IdentifierInfo *beforeInfo = nullptr;
            tok::TokenKind before = PP.getHookPrevious(1, beforeInfo);
            sized = before != tok::period && before != tok::arrow && before != tok::kw_struct &&
                    before != tok::kw_union && before != tok::kw_enum;
        }
        bool consumed = offerLowered(tok);
        if (sized) startLook();
        return consumed;
    }

    unsigned afterSized = 0; // tokens left to look at
    unsigned char mode = 0;  // the mode the lowering asked for (Preprocessor::HookWatched, HookBraces or HookAll)

    void startLook() {
        afterSized = 8;
        PP.setHookMode(Preprocessor::HookAll);
    }

    void endLook() {
        afterSized = 0;
        PP.setHookMode(mode);
    }

    // The token that the lowering's mode would have offered.
    bool wouldOffer(const Token &tok) const {
        return mode == Preprocessor::HookAll || tok.is(tok::eof) ||
               (tok.is(tok::identifier) && tok.getIdentifierInfo()->isHookWatched()) ||
               (mode == Preprocessor::HookBraces && tok.isOneOf(tok::l_brace, tok::r_brace));
    }

    // The token after a sized type name. False when the hook was not to be offered it.
    bool look(Token &tok) {
        bool offered = wouldOffer(tok);
        if (afterSized > 1 &&
            tok.isOneOf(tok::star, tok::kw_const, tok::kw_volatile, tok::kw_restrict, tok::kw__Atomic)) {
            afterSized -= 1;
            return offered;
        }
        endLook();
        keywordName(tok);
        return offered;
    }

    // A keyword used as a name: reported at the name, then it is an identifier (the keyword with a `_`), so that clang
    // reads the declaration it begins as one and has no cascade of errors to add.
    void keywordName(Token &tok) {
        const IdentifierInfo *info = tok.getIdentifierInfo();
        if (info == nullptr || tok.is(tok::identifier) || !tok.getLocation().isFileID()) return;
        StringRef word = info->getName();
        if (!smith_lang_misused_keyword(word.data(), word.size())) return;
        StringRef text = SM.getBufferData(MainFile);
        unsigned start = SM.getFileOffset(tok.getLocation());
        const char *noun = smith_lang_keyword_name_use(text.data(), text.size(), start, start + word.size());
        if (noun == nullptr) return;
        char message[512];
        if (smith_lang_keyword_message(message, sizeof message, word.data(), word.size(), noun) == 0) return;
        PP.getDiagnostics().Report(tok.getLocation(), diagnostic) << StringRef(message);
        IdentifierInfo *name = PP.getIdentifierInfo((word + "_").str());
        tok.setIdentifierInfo(name);
        tok.setKind(tok::identifier);
    }

    bool offerLowered(Token &tok) {
        // A pragma's annotation (`#pragma STDC FP_CONTRACT OFF`) is not a token of the program and has no spelling to
        // lower: it goes to the parser as it is.
        if (tok.isAnnotation() && tok.isNot(tok::annot_typename)) return false;
        if (!spelledInMain(tok.getLocation())) {
            if (tok.is(tok::eof)) return feed(tok);
            stats.outside += 1;
            return false;
        }
        if (tok.isNot(tok::eof) && sema != nullptr) {
            Fast fast = tryFast(tok);
            if (fast != Fast::No) {
                stats.fast += 1;
                return fast == Fast::Dropped;
            }
        }
        if (!stats.on) return keepCountof(tok, feed(tok));
        auto begin = std::chrono::steady_clock::now();
        bool consumed = keepCountof(tok, feed(tok));
        stats.slow_nanoseconds +=
            std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - begin).count();
        return consumed;
    }

    // The cases that the lowering answers without looking ahead, answered here without the machinery of a call: the
    // token is changed where it is (a prefixed name, a sized type), or a `static` goes before it.
    enum class Fast { No, Kept, Dropped };

    // The lowering's view of a token that the fast path offers: its kind and spelling, and what is known of its name.
    SmithToken lite(const Token &tok) {
        SmithToken out{};
        out.handle = smith_token_no_handle;
        out.location = tok.getLocation().getRawEncoding();
        out.text = "";
        if (const IdentifierInfo *info = tok.getIdentifierInfo()) {
            out.kind = SmithTokenKind_Identifier;
            out.text = info->getNameStart();
            out.length = info->getLength();
            out.watched = info->isHookWatched();
            out.declared = declaredByProgram(info);
        } else if (const char *punctuator = tok::getPunctuatorSpelling(tok.getKind())) {
            out.kind = SmithTokenKind_Punctuator;
            out.text = punctuator;
            out.length = punctuator[1] == 0 ? 1 : punctuator[2] == 0 ? 2 : 3; // spellings of 1 to 3 characters
        } else if (tok.isLiteral()) {
            out.kind = SmithTokenKind_Number;
        } else {
            out.kind = SmithTokenKind_Punctuator; // an annotation
        }
        return out;
    }

    // Whether a token before a name can end a type, so that the name may be a declarator (a question for the lowering).
    static bool mayEndType(tok::TokenKind kind) {
        return kind == tok::star || kind == tok::identifier || kind == tok::r_brace || kind == tok::annot_typename ||
               kind == tok::kw_const || kind == tok::kw_volatile || kind == tok::kw_restrict ||
               kind == tok::kw__Atomic || kind == tok::kw_char || kind == tok::kw_short || kind == tok::kw_int ||
               kind == tok::kw_long || kind == tok::kw_signed || kind == tok::kw_unsigned || kind == tok::kw_float ||
               kind == tok::kw_double || kind == tok::kw_void || kind == tok::kw_bool || kind == tok::kw__Bool ||
               kind == tok::kw__Complex || kind == tok::kw__BitInt || kind == tok::kw_typeof ||
               kind == tok::kw__Float16;
    }

    // Whether the name being read is where an enum's constant is declared: after the `{` of `enum {` or `enum Tag {`,
    // or after a `,` while the parser is inside an enum's definition. No type stands before it, so it is no declarator.
    bool enumConstantHere(tok::TokenKind before) {
        if (before == tok::comma)
            return sema != nullptr && sema->CurContext != nullptr && sema->CurContext->getDeclKind() == Decl::Enum;
        if (before != tok::l_brace) return false;
        const IdentifierInfo *info = nullptr;
        tok::TokenKind two = PP.getHookPrevious(2, info);
        if (two == tok::kw_enum) return true;
        return two == tok::identifier && PP.getHookPrevious(3, info) == tok::kw_enum;
    }

    // The tokens of a file that is not a module, or of a module's function body (the host offers the braces and the
    // names it watches): a module's name becomes its prefixed spelling, a sized type name its type, and the braces are
    // counted, with a few lookups and no call into the lowering.
    Fast tryFastSparse(Token &tok, bool body) {
        if (tok.isNot(tok::identifier)) {
            // A file with an `if constexpr` is offered every token of a body, not only its braces.
            return body && tok.isOneOf(tok::l_brace, tok::r_brace) &&
                           smith_lang_fast_brace(lang, &host, tok.is(tok::l_brace))
                       ? Fast::Kept
                       : Fast::No;
        }
        IdentifierInfo *info = tok.getIdentifierInfo();
        const IdentifierInfo *beforeInfo = nullptr;
        tok::TokenKind before = PP.getHookPrevious(1, beforeInfo);
        if (body) {
            auto renamed = renames.find(info);
            if (renamed != renames.end()) {
                if (smith_lang_import_name(lang, info->getNameStart(), info->getLength())) return Fast::No;
                bool parameter = smith_lang_param_name(lang, info->getNameStart(), info->getLength());
                if (before != tok::period && before != tok::arrow && !parameter) tok.setIdentifierInfo(renamed->second);
                return Fast::Kept;
            }
        }
        auto sized = sizedNames.find(info);
        if (sized == sizedNames.end()) return Fast::No;
        if (host.maybe_type_value)
            return Fast::No; // a sized name may be a type value: the lowering reads its neighbours
        if (before == tok::period || before == tok::arrow || before == tok::kw_struct || before == tok::kw_union ||
            before == tok::kw_enum) {
            return Fast::Kept;
        }
        if (declaredByProgram(info)) return Fast::No; // the program's own u32, perhaps
        if (mayEndType(before)) return Fast::No;
        if (enumConstantHere(before)) return Fast::Kept; // an enum's constant is the program's name
        if (!body && smith_lang_io_import(lang) && info->getName() == "i32") return Fast::No; // `i32 main(io::Io *io)`
        ensureSized(sized->second, tok.getLocation());
        return Fast::Kept;
    }

    // The declarations of a module, as they stream by: the lowering decides each token without looking ahead, or says
    // it must (Slow).
    Fast tryFast(Token &tok) {
        if (host.maybe_slice || smith_lang_slices_on(lang)) return Fast::No; // the lowering reads every token
        // A name of the module may hold a type (`constexpr type big = u32;`, then `big x = 1;`): the lowering reads its
        // neighbours, so it is not renamed here (issue #165).
        if (host.maybe_type_value && tok.is(tok::identifier) && renames.count(tok.getIdentifierInfo()) != 0)
            return Fast::No;
        bool body = smith_lang_body_sparse(lang);
        if (body || !smith_lang_is_module(lang)) return tryFastSparse(tok, body);
        SmithToken view = lite(tok);
        SmithLangAct act = smith_lang_fast(lang, &host, &view);
        switch (act.kind) {
        case SmithLangAct_Slow:
        case SmithLangAct_Done:
            return Fast::No;
        case SmithLangAct_Drop:
            return Fast::Dropped;
        case SmithLangAct_Rename: {
            auto renamed =
                renames.find(tok.getIdentifierInfo()); // declared and renamed already: no hashing of the text
            tok.setIdentifierInfo(renamed != renames.end() && renamed->second->getLength() == act.length
                                      ? renamed->second
                                      : PP.getIdentifierInfo(StringRef(act.text, act.length)));
            break;
        }
        case SmithLangAct_Sized:
            ensureSized(sizedNames.find(tok.getIdentifierInfo())->second, tok.getLocation());
            break;
        case SmithLangAct_Keep:
            break;
        }
        if (act.insert_static) putStaticBefore(tok);
        return Fast::Kept;
    }

    // A `static` for the parser, then the token: the token goes back on the stream, as the hook is not offered it again
    // (it is the token the source has at its location, so it is replayed, see below).
    void putStaticBefore(Token &tok) {
        Token keyword = tok;
        IdentifierInfo *info = PP.getIdentifierInfo("static");
        keyword.setIdentifierInfo(info);
        keyword.setKind(info->getTokenID());
        keyword.setFlag(Token::IsReinjected); // a token of nobody's: tools that record the parser's tokens skip it
        tok.clearFlag(Token::StartOfLine);
        tok.setFlag(Token::LeadingSpace);
        auto tokens = std::make_unique<Token[]>(1);
        tokens[0] = tok;
        replay[tok.getLocation().getRawEncoding()] += 1;
        PP.EnterTokenStream(std::move(tokens), 1, /*DisableMacroExpansion=*/false, /*IsReinject=*/false);
        tok = keyword;
    }

    // ---- Replay
    //
    // The parser reads the lowering's output as a stream the hook pushed back. A stream pushed back as a reinjection is
    // not reported to the Preprocessor's token watcher, which is how a tool that keeps the parser's tokens next to the
    // source's (clangd's TokenBuffer, behind hover, references, highlights and the outline) learns them. So the
    // output that stands for a token of the source, at its location, is pushed back as a plain stream and passes the
    // hook again without being lowered a second time (`replayed`); only what the source does not have (a `static`,
    // a symbol label, a prototype's copy) is reinjected. Without this, no token of an exported declaration's head, its
    // name included, was known to those tools.
    llvm::DenseMap<unsigned, unsigned>
        replay; // the locations of the tokens pushed back, until the hook is offered them

    bool replayed(const Token &tok) {
        if (replay.empty()) return false;
        if (tok.is(tok::eof)) {
            replay.clear();
            return false;
        }
        if (tok.getLocation().isInvalid()) return false;
        auto found = replay.find(tok.getLocation().getRawEncoding());
        if (found == replay.end()) return false;
        if (--found->second == 0) replay.erase(found);
        return true;
    }

    // Whether the parser has a declaration of the name that is the program's: not the typedef of a sized type that this
    // hook made, which the lowering must see as no declaration at all (the name is still the sized type).
    bool declaredByProgram(const IdentifierInfo *info) {
        if (info->getFETokenInfo() == nullptr) return false;
        if (sema == nullptr || !info->isHookWatched()) return true;
        auto sized = sizedNames.find(info);
        if (sized == sizedNames.end() || sizedDecls[sized->second] == nullptr) return true;
        auto top = sema->IdResolver.begin(info);
        return top == sema->IdResolver.end() || *top != sizedDecls[sized->second];
    }

    // u32 and the others are typedefs (`typedef __UINT32_TYPE__ u32;`), made at the first use of the name in a file
    // that declares nothing of that name, so that a diagnostic says `u32` (aka `unsigned int`). The tokens stay as
    // written: there is no macro to expand, and the typedef is implicit, so it is in no header.
    void ensureSized(unsigned index, SourceLocation at) {
        if (sizedDecls[index] != nullptr || sema == nullptr) return;
        IdentifierInfo *name = sizedInfos[index];
        if (name->getFETokenInfo() != nullptr) return; // the file or a header has its own
        QualType type = sizedQualType(index);
        if (type.isNull()) return;
        ASTContext &ast = sema->getASTContext();
        auto *decl = TypedefDecl::Create(ast, ast.getTranslationUnitDecl(), at, at, name,
                                         ast.getTrivialTypeSourceInfo(type, at));
        decl->setImplicit();
        ast.getTranslationUnitDecl()->addDecl(decl);
        sema->PushOnScopeChains(decl, sema->TUScope, /*AddToContext=*/false);
        sizedDecls[index] = decl;
    }

    QualType sizedQualType(unsigned index) {
        const SizedName &entry = sizedTable[index];
        ASTContext &ast = sema->getASTContext();
        if (entry.macro != nullptr) return sizedType(entry.macro);
        StringRef keyword(entry.keyword);
        return keyword == "float" ? ast.FloatTy : keyword == "double" ? ast.DoubleTy : ast.Float16Ty;
    }

  private:
    Preprocessor &PP;
    SourceManager &SM;
    FileID MainFile;
    unsigned mainBegin, mainEnd; // the raw source locations of the main file's text, the end included
    Sema *sema = nullptr;
    SmithLang *lang;
    SmithHost host{};
    std::string mainName;
    unsigned diagnostic = 0;
    // Per call.
    std::vector<Token> pool; // every token the lowering was given, by handle
    std::vector<Token> out;
    std::vector<bool> outReinjected; // per token of `out`
    std::vector<bool> outSynthetic;
    std::vector<uint32_t> outInstance; // per token of `out`: the generic instance it is in the body of (0: none)
    std::vector<std::string> outText;  // per token of `out` in an instance: its spelling
    std::vector<Action> actions;
    llvm::BumpPtrAllocator texts;
    llvm::DenseSet<unsigned> importFiles;                 // the FileIDs of the buffers that stand for `import <h>;`
    llvm::DenseMap<unsigned, std::string> importSpelling; // and how each imports its header: <stdio.h>, "mine.h"
    std::string fieldName;
    std::string fieldType;
    struct SizedName {
        const char *name;
        const char *macro;   // the compiler's macro for its type, or null
        const char *keyword; // else the keyword
    };
    static constexpr SizedName sizedTable[13] = {
        {"u8", "__UINT8_TYPE__", nullptr},   {"u16", "__UINT16_TYPE__", nullptr},
        {"u32", "__UINT32_TYPE__", nullptr}, {"u64", "__UINT64_TYPE__", nullptr},
        {"i8", "__INT8_TYPE__", nullptr},    {"i16", "__INT16_TYPE__", nullptr},
        {"i32", "__INT32_TYPE__", nullptr},  {"i64", "__INT64_TYPE__", nullptr},
        {"usize", "__SIZE_TYPE__", nullptr}, {"isize", "__PTRDIFF_TYPE__", nullptr},
        {"f16", nullptr, "_Float16"},        {"f32", nullptr, "float"},
        {"f64", nullptr, "double"}};
    llvm::DenseMap<const IdentifierInfo *, unsigned> sizedNames; // the sized type names, by IdentifierInfo
    IdentifierInfo *sizedInfos[13];
    NamedDecl *sizedDecls[13] = {};                                   // the typedefs this hook made
    llvm::DenseMap<const IdentifierInfo *, IdentifierInfo *> renames; // a module's names, to their prefixed spelling
    std::string headerBuffer;
    std::unique_ptr<llvm::MemoryBuffer> headerFile;

    bool spelledInMain(SourceLocation loc) const {
        if (loc.isInvalid()) return false;
        if (loc.isFileID()) {
            unsigned raw = loc.getRawEncoding(); // the main file's locations are one range
            return raw >= mainBegin && raw <= mainEnd;
        }
        return SM.getFileID(SM.getSpellingLoc(loc)) == MainFile;
    }

    // Whether a file was brought in by `import <h>;`: its include chain passes through one of our buffers.
    bool importOwned(FileID id) const {
        for (int depth = 0; depth < 64 && id.isValid(); depth++) { // include chains are short
            if (importFiles.contains(id.getHashValue())) return true;
            id = SM.getFileID(SM.getIncludeLoc(id));
        }
        return false;
    }

    // The spellings that are keywords of C23, which some SDK headers still define as macros (assert.h's static_assert):
    // they are the language's own, not a C name to write with `c::`.
    bool isStandardKeyword(StringRef name) const {
        if (!PP.getLangOpts().C23) return false;
        static constexpr StringRef keywords[] = {"static_assert", "alignas", "alignof", "bool",
                                                 "true",          "false",   "nullptr", "thread_local"};
        for (StringRef keyword : keywords) {
            if (name == keyword) return true;
        }
        return false;
    }

    // Whether the name at `at` is written `c::name`. A macro that is an argument of another is expanded with the
    // argument before the hook reaches the `c::` before it (`c::assert(n < c::SIZE_MAX)`), so the text is what says.
    bool writtenAsC(SourceLocation at) const {
        StringRef text = SM.getBufferData(SM.getFileID(at));
        size_t offset = SM.getFileOffset(at);
        auto space = [&](size_t i) { return text[i] == ' ' || text[i] == '\t' || text[i] == '\n'; };
        while (offset > 0 && space(offset - 1))
            offset -= 1;
        if (offset < 3 || text[offset - 1] != ':' || text[offset - 2] != ':') return false;
        offset -= 2;
        while (offset > 0 && space(offset - 1))
            offset -= 1;
        if (offset == 0 || text[offset - 1] != 'c') return false;
        return offset == 1 || !(isAlphanumeric(text[offset - 2]) || text[offset - 2] == '_');
    }

    // Whether the name at `at` follows `.` or `->`: a member's name, which no macro can be the use of
    // (`x.vector_length` where <simd/geometry.h> defines a macro of that name; the member is looked up in its struct).
    bool afterMemberAccess(SourceLocation at) const {
        StringRef text = SM.getBufferData(SM.getFileID(at));
        size_t offset = SM.getFileOffset(at);
        auto space = [&](size_t i) { return text[i] == ' ' || text[i] == '\t' || text[i] == '\n'; };
        while (offset > 0 && space(offset - 1))
            offset -= 1;
        if (offset >= 2 && text[offset - 1] == '>' && text[offset - 2] == '-') return true;
        return offset >= 1 && text[offset - 1] == '.' && !(offset >= 2 && text[offset - 2] == '.');
    }

    // A macro of an imported header used where the source names it with no `c::` is an error, in a module or not.
    void macroUsed(const MacroDefinition &definition, SourceRange range) {
        SourceLocation at = range.getBegin();
        const MacroInfo *macro = definition.getMacroInfo();
        if (!at.isFileID() || SM.getFileID(at) != MainFile || macro == nullptr || macro->isBuiltinMacro()) {
            return;
        }
        if (!importOwned(SM.getFileID(macro->getDefinitionLoc())) ||
            smith_lang_c_use_allowed(lang, at.getRawEncoding())) {
            return;
        }
        bool invalid = false;
        const char *start = SM.getCharacterData(at, &invalid);
        if (invalid) return;
        StringRef name(start, Lexer::MeasureTokenLength(at, SM, PP.getLangOpts()));
        if (isStandardKeyword(name) || writtenAsC(at) || afterMemberAccess(at)) return;
        std::string message = "`" + name.str() + "` is a C macro; write `c::" + name.str() + "`";
        PP.getDiagnostics().Report(at, diagnostic) << message;
    }

    class Macros final : public PPCallbacks {
      public:
        explicit Macros(SmithHook &hook) : hook(hook) {}
        void MacroExpands(const Token &, const MacroDefinition &definition, SourceRange range,
                          const MacroArgs *) override {
            hook.macroUsed(definition, range);
        }

      private:
        SmithHook &hook;
    };

    // Every name an imported header declared is watched, so a bare use of one in a module is offered to the lowering.
    static void watchImports(void *context) {
        auto *self = static_cast<SmithHook *>(context);
        if (self->sema == nullptr) return;
        for (auto &entry : self->PP.getIdentifierTable()) {
            IdentifierInfo *name = entry.second;
            if (name->getFETokenInfo() == nullptr) continue;
            for (auto at = self->sema->IdResolver.begin(name), end = self->sema->IdResolver.end(); at != end; ++at) {
                if (self->importOwned(self->SM.getFileID(self->SM.getFileLoc((*at)->getLocation())))) {
                    name->setHookWatched(true);
                    break;
                }
            }
        }
    }

    const char *keep(StringRef text) {
        char *copy = texts.Allocate<char>(text.size() + 1);
        std::memcpy(copy, text.data(), text.size());
        copy[text.size()] = 0;
        return copy;
    }

    SmithToken convert(const Token &tok, uint32_t handle) {
        SmithToken out{};
        out.handle = handle;
        out.location = tok.getLocation().getRawEncoding();
        out.line_start = tok.isAtStartOfLine();
        out.space_before = tok.hasLeadingSpace();
        out.foreign = tok.isNot(tok::eof) && !spelledInMain(tok.getLocation());
        if (tok.is(tok::eof)) {
            out.kind = SmithTokenKind_Eof;
            return out;
        }
        if (tok.isAnnotation()) {
            out.kind = SmithTokenKind_Punctuator;
            out.text = "";
            return out;
        }
        if (tok.is(tok::code_completion)) { // an editor's cursor, with what was typed of the name before it
            StringRef typed = PP.getCodeCompletionFilter();
            out.kind = SmithTokenKind_Punctuator;
            out.text = "";
            out.completion = true;
            out.partial = keep(typed);
            out.partial_length = typed.size();
            return out;
        }
        if (const char *punctuator = tok::getPunctuatorSpelling(tok.getKind())) {
            out.kind = SmithTokenKind_Punctuator;
            out.text = punctuator;
            out.length = punctuator[1] == 0 ? 1 : punctuator[2] == 0 ? 2 : 3; // spellings of 1 to 3 characters
            return out;
        }
        if (tok.isLiteral() || tok.is(tok::raw_identifier) || tok.getIdentifierInfo() != nullptr ||
            tok::getKeywordSpelling(tok.getKind()) != nullptr) {
            // Identifiers and keywords are their IdentifierInfo's name; a literal is read where it is written, unless
            // it has line splices or trigraphs to clean.
            if (const IdentifierInfo *info = tok.is(tok::raw_identifier) ? nullptr : tok.getIdentifierInfo()) {
                out.text = info->getNameStart();
                out.length = info->getLength();
                out.watched = info->isHookWatched();
                out.declared = declaredByProgram(info);
            } else if (tok.isLiteral() && tok.getLiteralData() != nullptr && !tok.needsCleaning()) {
                out.text = tok.getLiteralData();
                out.length = tok.getLength();
            } else {
                std::string spelling = PP.getSpelling(tok);
                out.text = keep(spelling);
                out.length = spelling.size();
            }
            out.kind = tok.is(tok::numeric_constant) ? SmithTokenKind_Number
                       : tok.isOneOf(tok::char_constant, tok::wide_char_constant, tok::utf8_char_constant,
                                     tok::utf16_char_constant, tok::utf32_char_constant)
                           ? SmithTokenKind_Character
                       : tok.isLiteral() ? SmithTokenKind_String
                                         : SmithTokenKind_Identifier;
            return out;
        }
        out.kind = SmithTokenKind_Punctuator;
        out.text = "";
        return out;
    }

    bool feed(Token &tok) {
        if (!smith_lang_holds(lang)) pool.clear(); // held tokens are re-emitted by handle
        out.clear();
        outReinjected.clear();
        outSynthetic.clear();
        outInstance.clear();
        outText.clear();
        actions.clear();
        texts.Reset();
        pool.push_back(tok);
        SmithToken first = convert(tok, pool.size() - 1);
        bool consumed = smith_lang_feed(lang, &host, &first);
        if (!consumed) {
            stats.kept += 1;
            return false;
        }
        if (actions.empty() && out.size() == 1 && replaceInPlace(tok, out[0])) {
            if (outReinjected[0]) tok.setFlag(Token::IsReinjected);
            stats.in_place += 1;
            return false;
        }
        if (smith_lang_holds(lang)) stats.held += 1;
        stats.streams += 1;
        flush();
        return true;
    }

    // The type a sized type name stands for, as the compiler's own: u32 is the target's unsigned int.
    QualType sizedType(StringRef macro) {
        ASTContext &ast = sema->getASTContext();
        const TargetInfo &target = ast.getTargetInfo();
        if (macro == "__SIZE_TYPE__") return ast.getCanonicalType(ast.getSizeType());
        if (macro == "__PTRDIFF_TYPE__") return ast.getCanonicalType(ast.getPointerDiffType());
        if (!macro.consume_front("__") || !macro.consume_back("_TYPE__")) return QualType();
        bool isSigned = macro.consume_front("INT");
        if (!isSigned && !macro.consume_front("UINT")) return QualType();
        unsigned width = 0;
        if (macro.getAsInteger(10, width)) return QualType();
        // The target's own choice, which its <stdint.h> macros are defined from: long long for 64 bits on macOS.
        TargetInfo::IntType choice = width == 16   ? (isSigned ? target.getInt16Type() : target.getUInt16Type())
                                     : width == 64 ? (isSigned ? target.getInt64Type() : target.getUInt64Type())
                                                   : target.getIntTypeByWidth(width, isSigned);
        switch (choice) {
        case TargetInfo::SignedChar:
            return ast.SignedCharTy;
        case TargetInfo::UnsignedChar:
            return ast.UnsignedCharTy;
        case TargetInfo::SignedShort:
            return ast.ShortTy;
        case TargetInfo::UnsignedShort:
            return ast.UnsignedShortTy;
        case TargetInfo::SignedInt:
            return ast.IntTy;
        case TargetInfo::UnsignedInt:
            return ast.UnsignedIntTy;
        case TargetInfo::SignedLong:
            return ast.LongTy;
        case TargetInfo::UnsignedLong:
            return ast.UnsignedLongTy;
        case TargetInfo::SignedLongLong:
            return ast.LongLongTy;
        case TargetInfo::UnsignedLongLong:
            return ast.UnsignedLongLongTy;
        default:
            return QualType();
        }
    }

    // One token out for the one in (maybe with some read ahead and dropped): the token is changed where it is, with no
    // stream to push back. A sized type name is left as written (a typedef), so no macro need expand.
    bool replaceInPlace(Token &tok, const Token &made) {
        if (!made.is(tok::identifier) || made.isExpandDisabled()) { // a keyword, or a name that needs no expansion
            tok = made;
            return true;
        }
        return false; // a macro of the program's (c::X), or a sized name with no Sema: expand it from a stream
    }

    // Puts the output back, last piece first, since each is pushed on top of the lexer stack.
    void flush() {
        closeTokens();
        relocate();
        for (size_t i = actions.size(); i > 0; i--) {
            Action &action = actions[i - 1];
            if (action.include) {
                enterHeader(action);
            } else {
                enterTokens(action);
            }
        }
    }

    // The tokens of one action, in runs of the same kind (see Replay), the last run first.
    void enterTokens(const Action &action) {
        size_t end = action.tokens.size();
        while (end > 0) {
            bool reinjected = action.reinjected[end - 1];
            size_t begin = end;
            while (begin > 0 && action.reinjected[begin - 1] == reinjected)
                begin -= 1;
            auto tokens = std::make_unique<Token[]>(end - begin);
            std::copy(action.tokens.begin() + static_cast<std::ptrdiff_t>(begin),
                      action.tokens.begin() + static_cast<std::ptrdiff_t>(end), tokens.get());
            if (!reinjected) {
                for (size_t i = begin; i < end; i++)
                    replay[action.tokens[i].getLocation().getRawEncoding()] += 1;
            }
            PP.EnterTokenStream(std::move(tokens), end - begin, /*DisableMacroExpansion=*/false, reinjected);
            end = begin;
        }
    }

    void closeTokens() {
        if (out.empty()) return;
        Action action;
        action.tokens = std::move(out);
        action.reinjected = std::move(outReinjected);
        action.synthetic = std::move(outSynthetic);
        action.instance = std::move(outInstance);
        action.text = std::move(outText);
        actions.push_back(std::move(action));
        out.clear();
        outReinjected.clear();
        outSynthetic.clear();
        outInstance.clear();
        outText.clear();
    }

    // ---- Relocation
    //
    // A prototype's copy of a head, and the tokens of a symbol label, would have the locations of the tokens of the
    // source they copy or follow, so that the declaration of the prototype stood where the definition does (every
    // reference, highlight and outline entry twice). They are given a place of their own instead: a buffer that holds
    // their text, which no tool reads as the file's, and `originOf` answers where each copies from. The diagnostics
    // there are the definition's again, so the Presenter leaves them out.
    llvm::DenseMap<unsigned, std::vector<std::pair<unsigned, SourceLocation>>> origins; // by buffer, by offset

    // ---- Generic instances
    //
    // The code of an instance is the generic's own tokens, so its places are the generic's source, and an error in it
    // must be reported there. But the generic's source is not parsed once: it is parsed for each instance, and an error
    // belongs to one. So the tokens of an instance get a buffer of their own, as a copy's do (`origins` says where
    // each token is from), and the buffer says which instance it is: the Presenter reports at the origin and adds a
    // note where the instance was asked for (and, for an instance asked for by another, where that was).
    struct InstanceInfo {
        unsigned parent;
        SourceLocation call;
        std::string description;
        SourceLocation definition; // the generic's name in its module's source
    };
    std::map<unsigned, InstanceInfo> instances;       // by id
    llvm::DenseMap<unsigned, unsigned> instanceFiles; // the buffer of an instance's tokens, to its id
    std::map<unsigned, FileID> instanceSources;       // and to the file it is a copy of
    std::map<std::string, FileID> placedFiles;

    static void instanceBegin(void *context, uint32_t id, uint32_t parent, uint32_t call, const char *description,
                              uint32_t definition) {
        auto *self = static_cast<SmithHook *>(context);
        self->instances[id] = {parent, SourceLocation::getFromRawEncoding(call), description,
                               SourceLocation::getFromRawEncoding(definition)};
    }

    // The file of a module's source, as the text the module has (a module found by its source is given to the file
    // manager as its interface, which has not the bodies of generics).
    FileID fileFor(const std::string &path) {
        auto known = placedFiles.find(path);
        if (known != placedFiles.end()) return known->second;
        FileID id;
        for (auto &[name, entry] : interfaces) {
            if (entry.ok && entry.source == path) {
                id = SM.createFileID(llvm::MemoryBuffer::getMemBufferCopy(entry.content, path), SrcMgr::C_User);
                break;
            }
        }
        if (id.isInvalid()) {
            if (auto file = PP.getFileManager().getFileRef(path)) {
                id = SM.getOrCreateFileID(*file, SrcMgr::C_User);
            } else {
                llvm::consumeError(file.takeError());
            }
        }
        placedFiles[path] = id;
        return id;
    }

    static uint32_t locatePlace(void *context, const char *file, uint32_t line, uint32_t column) {
        auto *self = static_cast<SmithHook *>(context);
        FileID id = self->fileFor(file);
        if (id.isInvalid()) return UINT32_MAX;
        SourceLocation at = self->SM.translateLineCol(id, line, column == 0 ? 1 : column);
        return at.isValid() ? at.getRawEncoding() : UINT32_MAX;
    }

    bool instanceNotes(SourceLocation at, std::vector<std::pair<SourceLocation, std::string>> &notes) override {
        if (!at.isFileID() || instanceFiles.empty()) return false;
        auto file = instanceFiles.find(SM.getFileID(at).getHashValue());
        if (file == instanceFiles.end()) return false;
        unsigned id = file->second;
        for (size_t depth = 0; id != 0 && depth < 40; depth++) { // the chain of instances that asked for this one
            auto found = instances.find(id);
            if (found == instances.end()) break;
            notes.emplace_back(found->second.call,
                               "in instance " + found->second.description + ", requested from here");
            id = found->second.parent;
        }
        return !notes.empty();
    }

    // Gives the tokens of each instance a buffer of its own, a copy of its generic's source file under the same name: a
    // token is at the offset it has in the source, so the debugger shows the generic's file, line and column (and the
    // checksum of the file), and a diagnostic is at the place in the source. The tokens that are no source's (the call
    // the instance is for, the instance's name) stand at the generic's name.
    void relocateInstances() {
        std::map<unsigned, std::vector<Token *>> byInstance;
        for (Action &action : actions) {
            for (size_t i = 0; i < action.tokens.size() && i < action.instance.size(); i++) {
                if (action.instance[i] != 0) byInstance[action.instance[i]].push_back(&action.tokens[i]);
            }
        }
        for (auto &[id, tokens] : byInstance) {
            SourceLocation definition = instances[id].definition;
            FileID source = definition.isValid() && definition.isFileID() ? SM.getFileID(definition) : FileID();
            if (source.isInvalid()) continue; // the generic's file is not known: the tokens stay where they were
            StringRef data = SM.getBufferData(source);
            FileID file = SM.createFileID(
                llvm::MemoryBuffer::getMemBuffer(data, SM.getBufferName(SM.getLocForStartOfFile(source)), false),
                SrcMgr::C_User);
            SourceLocation start = SM.getLocForStartOfFile(file);
            unsigned fallback = SM.getFileOffset(definition);
            for (Token *token : tokens) {
                SourceLocation origin = token->getLocation();
                bool inSource = origin.isFileID() && SM.getFileID(origin) == source;
                token->setLocation(
                    start.getLocWithOffset(static_cast<int>(inSource ? SM.getFileOffset(origin) : fallback)));
            }
            instanceFiles[file.getHashValue()] = id;
            instanceSources[file.getHashValue()] = source;
        }
    }

    void relocate() {
        relocateInstances();
        std::string text;
        std::vector<std::tuple<Token *, unsigned, SourceLocation>> spots; // the token, its offset, where it is from
        SourceLocation first;
        for (Action &action : actions) {
            for (size_t i = 0; i < action.tokens.size(); i++) {
                if (!action.synthetic[i] || (i < action.instance.size() && action.instance[i] != 0)) continue;
                Token &token = action.tokens[i];
                if (first.isInvalid()) first = token.getLocation();
                spots.emplace_back(&token, text.size(), token.getLocation());
                text += PP.getSpelling(token) + " ";
            }
        }
        if (spots.empty()) return;
        // The buffer is a file of the file manager's (a virtual one, with its text given): clangd places a declaration
        // by its file entry, and the first declaration of an exported function is this copy, so a call of it found no
        // place for it and go-to-definition said nothing (issue #154). The reply then names the definition's place.
        static unsigned copies = 0;
        std::string name = "<smith-copy-" + std::to_string(++copies) + ">";
        FileID id;
        if (OptionalFileEntryRef entry =
                PP.getFileManager().getVirtualFileRef(name, static_cast<off_t>(text.size()), 0)) {
            SM.overrideFileContents(*entry, llvm::MemoryBuffer::getMemBufferCopy(text, name));
            id = SM.createFileID(*entry, first, SrcMgr::C_User);
        } else {
            id = SM.createFileID(llvm::MemoryBuffer::getMemBufferCopy(text, "<smith-copy>"), SrcMgr::C_User, 0, 0,
                                 first);
        }
        SourceLocation start = SM.getLocForStartOfFile(id);
        std::vector<std::pair<unsigned, SourceLocation>> &list = origins[id.getHashValue()];
        for (auto &[token, offset, origin] : spots) {
            list.emplace_back(offset, origin);
            token->setLocation(start.getLocWithOffset(static_cast<int>(offset)));
        }
    }

    // Where the token at a location of a relocated copy was copied from; invalid for any other location.
    SourceLocation originOf(SourceLocation at) override {
        if (at.isFileID() && !instanceSources.empty()) { // a copy of a generic's source: the same place in the source
            auto copy = instanceSources.find(SM.getFileID(at).getHashValue());
            if (copy != instanceSources.end()) {
                return SM.getLocForStartOfFile(copy->second).getLocWithOffset(static_cast<int>(SM.getFileOffset(at)));
            }
        }
        if (!at.isFileID() || origins.empty()) return SourceLocation();
        auto found = origins.find(SM.getFileID(at).getHashValue());
        if (found == origins.end()) return SourceLocation();
        unsigned offset = SM.getFileOffset(at);
        SourceLocation origin;
        for (auto &[start, from] : found->second) {
            if (start > offset) break;
            origin = from;
        }
        return origin;
    }

    // ---- The source of a generated header
    //
    // `smith build` writes each module's header and an importer reads it, so a diagnostic about a name of the module
    // (a note: "'box::area' declared here") points into a file the module's author never wrote. The header says where
    // it came from and which module it is, and the lowering reads the module's source again to say where it declares
    // each export (smith_lang_declarations), which is stable where the lines of the header are not: an edit to the
    // body of a function must not rewrite the header, or every importer would be built again. The note goes to the
    // module's source, at the name. (An interface read in memory is the source's own file, with its declarations on
    // the lines of the module's, so it needs none of this.)
    struct Declared {
        bool tried = false;
        std::string path;
        std::map<std::string, std::pair<unsigned, unsigned>> places; // a member, to its line and column
    };
    std::map<std::string, Declared> declared; // by the path the header gives

    // The path of the source, and the module, that a generated header says; false for any other text.
    static bool generatedBy(StringRef text, std::string &source, std::string &module) {
        // the generator holds the lines it writes whole against a formatter, with a first line that says so
        if (text.starts_with("// clang-format off\n")) text = text.drop_front(strlen("// clang-format off\n"));
        StringRef first = text.take_until([](char c) { return c == '\n'; });
        if (!first.consume_front("// Generated by smith from ") || !first.consume_back(". Do not edit.")) return false;
        StringRef rest = text.drop_front(text.find('\n') + 1);
        StringRef second = rest.take_until([](char c) { return c == '\n'; });
        if (!second.consume_front("// smith-module: ")) return false;
        source = first.str();
        module = second.str();
        return true;
    }

    Declared &placesOf(const std::string &source) {
        Declared &entry = declared[source];
        if (entry.tried) return entry;
        entry.tried = true;
        for (const std::string &path : {source, absolute(source)}) {
            auto buffer = PP.getFileManager().getVirtualFileSystem().getBufferForFile(path);
            if (!buffer) continue;
            entry.path = path;
            std::string out(1 << 16, '\0');
            size_t used = smith_lang_declarations(path.c_str(), (*buffer)->getBufferStart(), (*buffer)->getBufferSize(),
                                                  out.data(), out.size());
            StringRef lines(out.data(), used);
            while (!lines.empty()) {
                auto [line, more] = lines.split('\n');
                lines = more;
                SmallVector<StringRef, 3> words;
                line.split(words, ' ');
                unsigned number = 0, column = 0;
                if (words.size() == 3 && !words[1].getAsInteger(10, number) && !words[2].getAsInteger(10, column)) {
                    entry.places[words[0].str()] = {number, column};
                }
            }
            break;
        }
        return entry;
    }

    bool generatedAt(SourceLocation at) override {
        if (!at.isFileID() || SM.getFileID(at) == MainFile) return false;
        FileID file = SM.getFileID(at);
        bool invalid = false;
        StringRef text = SM.getBufferData(file, &invalid);
        std::string source, module;
        if (invalid) return false;
        if (generatedBy(text, source, module)) return true;
        // A module's source read as its interface: the one .smith file that is not the file being compiled.
        OptionalFileEntryRef entry = SM.getFileEntryRefForID(file);
        return entry && entry->getName().ends_with(".smith");
    }

    SourceLocation sourceOf(SourceLocation at) override {
        if (!at.isFileID() || SM.getFileID(at) == MainFile) return SourceLocation();
        FileID file = SM.getFileID(at);
        bool invalid = false;
        StringRef text = SM.getBufferData(file, &invalid);
        std::string source, module;
        if (invalid || !generatedBy(text, source, module)) return SourceLocation();
        unsigned offset = SM.getFileOffset(at);
        size_t end = offset;
        while (end < text.size() && (std::isalnum((unsigned char)text[end]) || text[end] == '_'))
            end += 1;
        std::string prefix = module;
        for (size_t i; (i = prefix.find("::")) != std::string::npos;)
            prefix.replace(i, 2, "_");
        prefix += "_";
        StringRef word = text.slice(offset, end);
        if (!word.consume_front(prefix)) return SourceLocation();
        Declared &entry = placesOf(source);
        auto place = entry.places.find(word.str());
        if (place == entry.places.end()) return SourceLocation();
        auto found = PP.getFileManager().getFileRef(entry.path);
        if (!found) {
            llvm::consumeError(found.takeError());
            return SourceLocation();
        }
        return SM.translateLineCol(SM.getOrCreateFileID(*found, SrcMgr::C_User), place->second.first,
                                   place->second.second);
    }

    void enterHeader(const Action &action) {
        if (action.literal) { // the copies of an import's functions: a system file, so that no check of the file reads
                              // them
            auto buffer = llvm::MemoryBuffer::getMemBufferCopy(action.body, "<smith-bodies>");
            FileID id = SM.createFileID(std::move(buffer), SrcMgr::C_System, 0, 0, action.at);
            importFiles.insert(id.getHashValue());
            PP.EnterSourceFile(id, nullptr, action.at);
            return;
        }
        // A module's header has a section for the instances of its exported generics (what they use of its private
        // code): only an importer that is a .smith file reads it.
        std::string text = action.preface + (action.angled ? "" : "#define SMITH_INSTANCES\n") +
                           std::string("#include ") + (action.angled ? "<" : "\"") + action.header +
                           (action.angled ? ">" : "\"") + "\n" + (action.angled ? "" : "#undef SMITH_INSTANCES\n");
        auto buffer = llvm::MemoryBuffer::getMemBufferCopy(text, "<smith-import>");
        FileID id = SM.createFileID(std::move(buffer), SrcMgr::C_User, 0, 0, action.at);
        importFiles.insert(id.getHashValue());
        importSpelling[id.getHashValue()] = (action.angled ? "<" : "\"") + action.header + (action.angled ? ">" : "\"");
        PP.EnterSourceFile(id, nullptr, action.at);
    }

    static void nextToken(void *context, SmithToken *token, bool unexpanded) {
        auto *self = static_cast<SmithHook *>(context);
        Token next;
        if (unexpanded) {
            self->PP.LexUnexpandedToken(next);
        } else {
            self->PP.Lex(next);
        }
        self->pool.push_back(next);
        *token = self->convert(next, self->pool.size() - 1);
    }

    Token makeToken(const SmithToken *token) {
        Token made;
        made.startToken();
        size_t length = token->length;
        char *text = texts.Allocate<char>(length + 1);
        std::memcpy(text, token->text, length);
        text[length] = 0;
        if (token->kind == SmithTokenKind_Identifier) {
            made.setKind(tok::raw_identifier);
            made.setRawIdentifierData(text);
            made.setLength(length);
            PP.LookUpIdentifierInfo(made);
        } else {
            Lexer raw(SourceLocation(), PP.getLangOpts(), text, text, text + length);
            raw.LexFromRawLexer(made);
        }
        return made;
    }

    // Whether the token is an identifier that no macro is defined for.
    bool namesNoMacro(const Token &token) {
        const IdentifierInfo *info = token.getIdentifierInfo();
        return token.is(tok::identifier) && info != nullptr && !info->hasMacroDefinition();
    }

    // A `countof` the lowering passed on as it is, with no meaning of its own, is C2y's (issue #167).
    bool keepCountof(Token &tok, bool consumed) {
        const IdentifierInfo *beforeInfo = nullptr;
        tok::TokenKind before = PP.getHookPrevious(1, beforeInfo);
        if (!consumed && tok.is(tok::identifier)) deferToKeyword(tok, before);
        if (!consumed && tok.is(tok::identifier) && before != tok::period && before != tok::arrow &&
            !mayEndType(before)) { // after a type it declares a name of the program's own
            countofToKeyword(tok);
        }
        return consumed;
    }

    // The identifier `countof` of the lowering's output, as the keyword `_Countof`, unless it names a member
    // (`x.countof`) or something the program declared.
    // In a module `defer` is the keyword of C2y's defer TS, `_Defer` to the compiler (issue #210); it names nothing
    // (a type before it makes it a declarator), and a member `x.defer` is the member's. Without a `module` line it is
    // an ordinary identifier.
    void deferToKeyword(Token &made, tok::TokenKind before) {
        const IdentifierInfo *info = made.getIdentifierInfo();
        if (info == nullptr || info->getName() != "defer" || !smith_lang_is_module(lang)) return;
        if (before == tok::period || before == tok::arrow) return;
        if (mayEndType(before)) {
            PP.getDiagnostics().Report(made.getLocation(), diagnostic)
                << StringRef("`defer` is a keyword in a module (C2y's defer): it cannot name a variable or a function");
            return;
        }
        IdentifierInfo *keyword = PP.getIdentifierInfo("_Defer");
        made.setIdentifierInfo(keyword);
        made.setKind(keyword->getTokenID());
    }

    void countofToKeyword(Token &made) {
        const IdentifierInfo *info = made.getIdentifierInfo();
        if (info->getName() != "countof" || info->getFETokenInfo() != nullptr) return;
        if (!out.empty() && (out.back().isOneOf(tok::period, tok::arrow) || mayEndType(out.back().getKind()))) return;
        IdentifierInfo *keyword = PP.getIdentifierInfo("_Countof");
        made.setIdentifierInfo(keyword);
        made.setKind(keyword->getTokenID());
    }

    static void emitToken(void *context, const SmithToken *token) {
        auto *self = static_cast<SmithHook *>(context);
        Token made;
        if (token->handle != smith_token_no_handle) {
            made = self->pool[token->handle];
        } else {
            made = self->makeToken(token);
        }
        made.setLocation(SourceLocation::getFromRawEncoding(token->location));
        bool named = false; // made the sized type's own name
        if (token->expand && token->handle == smith_token_no_handle && self->sema != nullptr) {
            // A sized type name the lowering made into the compiler's name for the type (__UINT32_TYPE__): it stays the
            // name as written, a typedef, so that no macro expands and a diagnostic names the type as the program did.
            for (unsigned i = 0; i < 13; i++) {
                const SizedName &entry = sizedTable[i];
                if (token->length == strlen(entry.macro ? entry.macro : entry.keyword) &&
                    memcmp(token->text, entry.macro ? entry.macro : entry.keyword, token->length) == 0) {
                    self->ensureSized(i, made.getLocation());
                    made.setIdentifierInfo(self->sizedInfos[i]);
                    made.setKind(tok::identifier);
                    named = true;
                    break;
                }
            }
        }
        // `countof` left in the output is C2y's, on a fixed array (`u32 xs[4]`, a local or a member): the lowering took
        // every other meaning (a slice, a pack, fields, an array parameter, a tuple). A name of the program's own
        // keeps its meaning (issue #167).
        if (!token->expand && made.is(tok::identifier) && self->sema != nullptr) self->countofToKeyword(made);
        if (!token->expand && made.is(tok::identifier) && self->sema != nullptr)
            self->deferToKeyword(made, self->out.empty() ? tok::unknown : self->out.back().getKind());
        made.clearFlag(Token::StartOfLine);
        made.clearFlag(Token::LeadingSpace);
        made.clearFlag(Token::DisableExpand);
        if (token->line_start) made.setFlag(Token::StartOfLine);
        if (token->space_before) made.setFlag(Token::LeadingSpace);
        if ((!token->expand || named) && made.getIdentifierInfo() != nullptr) made.setFlag(Token::DisableExpand);
        // Replayed (see there): what stands for a token of the source, where the source has it.
        // A `c::name` that is no macro stays a plain name in the main file, so that tools see the name the source has
        // there; a macro is expanded from a stream of its own, which they do not.
        bool expandsMacro = token->expand && !named && !self->namesNoMacro(made);
        bool real = !token->synthetic && !expandsMacro && made.isNot(tok::eof) && made.getLocation().isFileID() &&
                    self->spelledInMain(made.getLocation());
        self->out.push_back(made);
        self->outReinjected.push_back(!real);
        self->outSynthetic.push_back(token->copy && made.getLocation().isFileID() &&
                                     self->spelledInMain(made.getLocation()));
        self->outInstance.push_back(token->instance);
        self->outText.push_back(token->instance != 0 ? std::string(token->text, token->length) : std::string());
    }

    static void includeHeader(void *context, const char *header, bool angled) {
        auto *self = static_cast<SmithHook *>(context);
        self->closeTokens();
        Action action;
        action.include = true;
        action.angled = angled;
        action.header = header;
        if (!angled && self->includePathOf(header, action.header, action.preface)) {
            // A module read from its source: its interface is the text of that file, as far as this compile goes.
        } else if (!angled) { // a quoted header is found beside the file that imports it, as #include "h" would
            llvm::SmallString<256> beside(llvm::sys::path::parent_path(self->mainName));
            llvm::sys::path::append(beside, header);
            if (llvm::sys::fs::exists(beside)) action.header = std::string(beside);
        }
        action.at = self->pool.empty() ? SourceLocation() : self->pool.front().getLocation();
        self->actions.push_back(std::move(action));
    }

    static void reportError(void *context, uint32_t location, const char *message) {
        auto *self = static_cast<SmithHook *>(context);
        self->PP.getDiagnostics().Report(SourceLocation::getFromRawEncoding(location), self->diagnostic)
            << StringRef(message);
    }

    // The declaration a name means at this point of the parse, or null.
    NamedDecl *findDecl(const char *name, uint32_t length, SmithLookupKind space) {
        if (sema == nullptr || sema->getCurScope() == nullptr) return nullptr;
        IdentifierInfo *id = PP.getIdentifierInfo(StringRef(name, length));
        if (id->getFETokenInfo() == nullptr) return nullptr; // nothing is declared with that name
        LookupResult found(*sema, id, SourceLocation(),
                           space == SmithLookup_Tag ? Sema::LookupTagName : Sema::LookupOrdinaryName);
        if (!sema->LookupName(found, sema->getCurScope()) || found.empty()) return nullptr;
        return found.getRepresentativeDecl();
    }

    // The complete type a typedef or tag names, or a null type.
    QualType completeType(const char *name, uint32_t length, SmithLookupKind space) {
        auto *decl = dyn_cast_or_null<TypeDecl>(findDecl(name, length, space));
        if (decl == nullptr) return QualType();
        QualType type = sema->getASTContext().getTypeDeclType(decl);
        if (type.isNull() || type->isIncompleteType() || type->isDependentType()) return QualType();
        return type;
    }

    static bool typeInfo(void *context, const char *name, uint32_t length, SmithLookupKind space, SmithTypeInfo *info) {
        auto *self = static_cast<SmithHook *>(context);
        QualType type = self->completeType(name, length, space);
        if (type.isNull()) return false;
        ASTContext &ast = self->sema->getASTContext();
        info->size = ast.getTypeSizeInChars(type).getQuantity();
        info->alignment = ast.getTypeAlignInChars(type).getQuantity();
        info->fields = 0;
        info->is_record = false;
        if (const RecordDecl *record = type->getAsRecordDecl()) {
            info->is_record = true;
            info->fields = std::distance(record->field_begin(), record->field_end());
        }
        return true;
    }

    // ---- A type written as tokens
    //
    // A generic's instances are told apart by their types, and its `if constexpr` asks what a type is. The type comes
    // as the tokens the call wrote, lowered (`const geometry_Point *`, `__UINT32_TYPE__`), and the parser reads them.
    std::string queryText;
    std::string pointeeText; // the pointee a type query names (kept until the next query)

    // The type a spelling names (`u32`, `struct Point *`, `int (*)(int)`), read by the parser as a type name in the
    // scope it is in, or a null type when it is not one. Diagnostics are off while it reads.
    QualType readType(StringRef spelling) {
        if (!sema->ParseTypeFromStringCallback || sema->getCurScope() == nullptr) return QualType();
        std::string text = spelling.str();
        nameSizedTypes(text);
        DiagnosticsEngine &diags = sema->getDiagnostics();
        bool wasSuppressed = diags.getSuppressAllDiagnostics();
        diags.setSuppressAllDiagnostics(true);
        DiagnosticErrorTrap trap(diags);
        TypeResult parsed = sema->ParseTypeFromStringCallback(text, "<smith>", SourceLocation());
        bool failed = trap.hasErrorOccurred();
        diags.setSuppressAllDiagnostics(wasSuppressed);
        if (failed || parsed.isInvalid() || !parsed.get()) return QualType();
        return sema->GetTypeFromParser(parsed.get());
    }

    static bool typeQuery(void *context, const char *spelling, uint32_t length, SmithTypeQuery *out) {
        auto *self = static_cast<SmithHook *>(context);
        if (self->sema == nullptr) return false;
        QualType type = self->readType(StringRef(spelling, length));
        if (type.isNull()) return false;
        ASTContext &ast = self->sema->getASTContext();
        QualType canonical = type.getCanonicalType();
        *out = SmithTypeQuery{};
        self->queryText = canonical.getAsString(ast.getPrintingPolicy());
        out->canonical = self->queryText.c_str();
        out->canonical_length = self->queryText.size();
        if (canonical->isPointerType()) { // the pointee, for a pointer's handle (`typeof(*(t)nullptr)`)
            self->pointeeText = canonical->getPointeeType().getCanonicalType().getAsString(ast.getPrintingPolicy());
            out->pointee = self->pointeeText.c_str();
            out->pointee_length = self->pointeeText.size();
        }
        out->is_bool = canonical->isBooleanType();
        out->is_enum = canonical->isEnumeralType();
        out->is_integer = canonical->isIntegerType() && !out->is_bool && !out->is_enum;
        out->is_float = canonical->isRealFloatingType();
        out->is_signed = out->is_float || canonical->isSignedIntegerOrEnumerationType();
        out->is_pointer = canonical->isPointerType();
        out->is_record = canonical->isRecordType();
        out->is_array = canonical->isArrayType();
        out->complete = !canonical->isIncompleteType() && !canonical->isDependentType() && !canonical->isVoidType() &&
                        !canonical->isFunctionType();
        if (out->complete) {
            out->size = ast.getTypeSizeInChars(canonical).getQuantity();
            out->alignment = ast.getTypeAlignInChars(canonical).getQuantity();
        }
        return true;
    }

    // ---- The type of an expression, asked of the parser

    // The type an argument has once it is passed: an array or a function as a pointer, and no qualifiers.
    QualType finishType(QualType type) {
        ASTContext &ast = sema->getASTContext();
        if (type->isArrayType()) return ast.getPointerType(ast.getAsArrayType(type)->getElementType());
        if (type->isFunctionType()) return ast.getPointerType(type);
        return type.getUnqualifiedType();
    }

    // The type of the expression the tokens spell, asked of the parser itself: `__typeof__(tokens)` is read through
    // Sema's ParseTypeFromString (the parser's own entry for a type written in a string: it saves the current token,
    // reads the string's tokens in a child scope of the one the parser is in, so the locals declared so far are seen,
    // and puts the token back). The operand of typeof is never evaluated, so nothing is marked used. Diagnostics are
    // off while it runs, and asked again with them on when the parse fails, so the compiler says why itself.
    bool parseTypeof(const std::string &text, QualType *type, const Expr **expression) {
        if (!sema->ParseTypeFromStringCallback || sema->getCurScope() == nullptr) return false;
        std::string source = "__typeof__(" + text + ")";
        DiagnosticErrorTrap trap(sema->getDiagnostics());
        TypeResult parsed = sema->ParseTypeFromStringCallback(source, "<smith>", SourceLocation());
        if (trap.hasErrorOccurred() || parsed.isInvalid() || !parsed.get()) {
            // typeof of a bit-field is an error in C, and the field is a value like any other (`fields(T)`): it is
            // typed as the integer it promotes to. Only a member access can be one, and what the retry says is not
            // shown: if it fails too, the first parse's own message is the compiler's.
            if (text.find('.') == std::string::npos && text.find("->") == std::string::npos) return false;
            DiagnosticsEngine &diags = sema->getDiagnostics();
            bool wasSuppressed = diags.getSuppressAllDiagnostics();
            diags.setSuppressAllDiagnostics(true);
            DiagnosticErrorTrap again(diags);
            std::string promoted = "__typeof__((" + text + ") + 0)";
            parsed = sema->ParseTypeFromStringCallback(promoted, "<smith>", SourceLocation());
            bool failed = again.hasErrorOccurred() || parsed.isInvalid() || !parsed.get();
            diags.setSuppressAllDiagnostics(wasSuppressed);
            if (failed) return false;
        }
        QualType found = sema->GetTypeFromParser(parsed.get());
        const auto *typeOf = found.isNull() ? nullptr : dyn_cast<TypeOfExprType>(found.getTypePtr());
        if (typeOf == nullptr || typeOf->getUnderlyingExpr() == nullptr ||
            typeOf->getUnderlyingExpr()->containsErrors()) {
            return false;
        }
        *expression = typeOf->getUnderlyingExpr();
        *type = (*expression)->getType();
        return !type->isNull();
    }

    // The lowering writes a sized type as the compiler's macro for it (`__UINT32_TYPE__`), which the program's `u32` is
    // a typedef of: the text is read with the typedef, made if the program has not named it yet.
    void nameSizedTypes(std::string &text) {
        for (size_t at = 0; at < text.size();) {
            size_t end = at;
            while (end < text.size() && isAsciiIdentifierContinue(text[end]))
                end += 1;
            if (end == at || isDigit(text[at])) {
                at = end == at ? at + 1 : end;
                continue;
            }
            for (unsigned i = 0; i < 13; i++) {
                const SizedName &entry = sizedTable[i];
                bool named = text.compare(at, end - at, entry.name) == 0;
                bool macro = !named && entry.macro != nullptr && text.compare(at, end - at, entry.macro) == 0;
                if (!named && !macro) continue;
                ensureSized(i, SM.getLocForStartOfFile(MainFile));
                if (macro) text.replace(at, end - at, entry.name);
                end = at + strlen(entry.name);
                break;
            }
            at = end;
        }
    }

    // The number an expression folds to, when it does: an integer as a sign and a magnitude, a float as a double.
    static void foldedValue(const Expr *expression, ASTContext &ast, SmithExprType *out) {
        Expr::EvalResult result;
        if (!expression->EvaluateAsRValue(result, ast) || result.HasSideEffects) return;
        if (result.Val.isInt()) {
            const llvm::APSInt &number = result.Val.getInt();
            llvm::APInt wide =
                number.isSigned() ? number.sext(number.getBitWidth() + 1) : number.zext(number.getBitWidth() + 1);
            out->constant = true;
            out->negative = wide.isNegative();
            if (out->negative) wide.negate();
            out->overflow = wide.getActiveBits() > 64;
            out->magnitude = wide.getLimitedValue();
        } else if (result.Val.isFloat()) {
            out->constant = true;
            out->is_float = true;
            out->real = result.Val.getFloat().convertToDouble();
        }
    }

    static bool exprType(void *context, const char *tokens, uint32_t length, SmithExprType *out) {
        auto *self = static_cast<SmithHook *>(context);
        bool explain = out->explain;
        *out = SmithExprType{};
        if (self->sema == nullptr) return false;
        std::string text;
        for (uint32_t at = 0; at < length; at++)
            text += tokens[at] == '\x1f' ? ' ' : tokens[at];
        self->nameSizedTypes(text);
        DiagnosticsEngine &diags = self->sema->getDiagnostics();
        bool wasSuppressed = diags.getSuppressAllDiagnostics();
        diags.setSuppressAllDiagnostics(true);
        QualType type;
        const Expr *expression = nullptr;
        bool good = self->parseTypeof(text, &type, &expression);
        diags.setSuppressAllDiagnostics(wasSuppressed);
        if (!good) {
            self->queryText = "the compiler does not accept it here";
            if (explain) { // the compiler's own words for why: the error fails the compile, as the lowering's does
                DiagnosticCollector collector;
                std::unique_ptr<DiagnosticConsumer> owned = diags.takeClient();
                DiagnosticConsumer *client = diags.getClient();
                diags.setClient(&collector, false);
                self->parseTypeof(text, &type, &expression);
                diags.setClient(client, owned.release() != nullptr);
                if (!collector.first.empty()) self->queryText = collector.first;
            }
            out->why = self->queryText.c_str();
            return false;
        }
        ASTContext &ast = self->sema->getASTContext();
        foldedValue(expression, ast, out);
        out->is_function = type->isFunctionType();
        if (const auto *constant = ast.getAsConstantArrayType(type)) {
            out->is_array = true;
            out->array_size = constant->getSize().getZExtValue();
            QualType inner = type;
            while (const auto *level = ast.getAsConstantArrayType(inner)) {
                if (out->array_rank < 8) out->array_dims[out->array_rank] = level->getSize().getZExtValue();
                out->array_rank += 1;
                inner = level->getElementType();
            }
            snprintf(out->array_element, sizeof out->array_element, "%s",
                     inner.getUnqualifiedType().getAsString(ast.getPrintingPolicy()).c_str());
        }
        type = self->finishType(type);
        const PrintingPolicy &policy = ast.getPrintingPolicy();
        snprintf(out->written, sizeof out->written, "%s", type.getAsString(policy).c_str());
        snprintf(out->canonical, sizeof out->canonical, "%s", type.getCanonicalType().getAsString(policy).c_str());
        return true;
    }

    // The value tree of an evaluated constant (runtime/lang.h SmithValue), its nodes taken from the caller's pool. The
    // reason it cannot be one (a pointer, a union, a wide integer) in `why`.
    static SmithValue *takeNodes(SmithValueQuery *out, uint32_t count, std::string &why) {
        if (out->used + count > out->capacity) {
            why = "the value is too large to read back";
            return nullptr;
        }
        SmithValue *nodes = out->nodes + out->used;
        out->used += count;
        for (uint32_t i = 0; i < count; i++)
            nodes[i] = SmithValue{};
        return nodes;
    }

    static bool readValue(const APValue &value, QualType type, SmithValueQuery *out, SmithValue *node, std::string &why,
                          unsigned depth) {
        if (depth > 32) {
            why = "the value is nested too deeply to read back";
            return false;
        }
        type = type.getCanonicalType();
        if (value.isInt()) {
            const llvm::APSInt &number = value.getInt();
            if (number.getBitWidth() > 64 && (number.isSigned() || number.getBitWidth() > 128)) {
                why = "a signed integer, or one wider than 128 bits, is not a value smith reads back";
                return false;
            }
            // An `unsigned __int128` is a type's handle (a pointer's carries its pointee's in the upper half): the
            // lowering reads the lower half, which holds the index, the slice depth and the facts of the type.
            if (number.getBitWidth() > 64) {
                node->kind = SmithValue_Int;
                node->bits = number.getBitWidth();
                node->magnitude = number.trunc(64).getZExtValue();
                return true;
            }
            llvm::APInt wide =
                number.isSigned() ? number.sext(number.getBitWidth() + 1) : number.zext(number.getBitWidth() + 1);
            node->kind = SmithValue_Int;
            node->is_signed = number.isSigned();
            node->negative = wide.isNegative();
            if (node->negative) wide.negate();
            node->bits = number.getBitWidth();
            node->magnitude = wide.getLimitedValue();
            return true;
        }
        if (value.isFloat()) {
            const llvm::fltSemantics &semantics = value.getFloat().getSemantics();
            bool single = &semantics == &llvm::APFloat::IEEEsingle();
            if (!single && &semantics != &llvm::APFloat::IEEEdouble()) {
                why = "only an f32 or an f64 is a float value";
                return false;
            }
            node->kind = SmithValue_Float;
            node->is_f32 = single;
            node->real = value.getFloat().convertToDouble();
            return true;
        }
        if (value.isStruct()) {
            const RecordDecl *record = type->getAsRecordDecl();
            if (record == nullptr) {
                why = "the value is a struct the compiler cannot name";
                return false;
            }
            std::vector<const FieldDecl *> fields;
            for (const FieldDecl *field : record->fields()) {
                if (field->isUnnamedBitField()) continue;
                fields.push_back(field);
            }
            node->kind = SmithValue_Struct;
            node->count = fields.size();
            node->items = fields.empty() ? nullptr : takeNodes(out, node->count, why);
            if (!fields.empty() && node->items == nullptr) return false;
            for (size_t i = 0; i < fields.size(); i++) {
                if (!readValue(value.getStructField(fields[i]->getFieldIndex()), fields[i]->getType(), out,
                               &node->items[i], why, depth + 1)) {
                    return false;
                }
            }
            return true;
        }
        if (value.isUnion()) {
            why = "a union is not a value smith reads back";
            return false;
        }
        if (value.isArray()) {
            const auto *array = type->getAsArrayTypeUnsafe();
            if (array == nullptr) return false;
            unsigned size = value.getArraySize();
            node->kind = SmithValue_Array;
            node->count = size;
            node->items = size == 0 ? nullptr : takeNodes(out, size, why);
            if (size != 0 && node->items == nullptr) return false;
            for (unsigned i = 0; i < size; i++) {
                const APValue &element =
                    i < value.getArrayInitializedElts() ? value.getArrayInitializedElt(i) : value.getArrayFiller();
                if (!readValue(element, array->getElementType(), out, &node->items[i], why, depth + 1)) return false;
            }
            return true;
        }
        if (value.isLValue() && !value.isNullPointer() && value.getLValueOffset().isZero()) {
            // A pointer to a whole string literal (the name of a `Field`) is read as the array of its characters.
            const auto *base = value.getLValueBase().dyn_cast<const Expr *>();
            const auto *literal = base == nullptr ? nullptr : dyn_cast<StringLiteral>(base->IgnoreParenImpCasts());
            if (literal != nullptr && literal->getCharByteWidth() == 1) {
                unsigned size = literal->getLength();
                node->kind = SmithValue_Array;
                node->count = size;
                node->items = size == 0 ? nullptr : takeNodes(out, size, why);
                if (size != 0 && node->items == nullptr) return false;
                for (unsigned i = 0; i < size; i++) {
                    node->items[i].kind = SmithValue_Int;
                    node->items[i].bits = 8;
                    node->items[i].magnitude = literal->getCodeUnit(i);
                }
                return true;
            }
        }
        if (value.isLValue() || value.isMemberPointer()) {
            why = "a pointer is not a value (C23: a `constexpr` pointer must be null)";
            return false;
        }
        why = "the compiler's value is not an integer, a float, a struct or an array";
        return false;
    }

    // The text of why the compiler did not fold an expression: its own notes, as the parser would print them.
    std::string notConstant(const Expr *expression, ASTContext &ast) {
        Expr::EvalResult result;
        SmallVector<PartialDiagnosticAt, 8> notes;
        result.Diag = &notes;
        if (expression->EvaluateAsRValue(result, ast, /*InConstantContext=*/true) && !result.HasSideEffects) return "";
        std::string message = "the expression is not a constant expression";
        for (const PartialDiagnosticAt &note : notes) {
            SmallString<256> text;
            note.second.EmitToString(sema->getDiagnostics(), text);
            message += "; " + text.str().str();
        }
        return message;
    }

    static bool valueQuery(void *context, const char *tokens, uint32_t length, SmithValueQuery *out) {
        auto *self = static_cast<SmithHook *>(context);
        out->used = 0;
        out->root = SmithValue{};
        out->why = nullptr;
        if (self->sema == nullptr) return false;
        std::string text;
        for (uint32_t at = 0; at < length; at++)
            text += tokens[at] == '\x1f' ? ' ' : tokens[at];
        self->nameSizedTypes(text);
        DiagnosticsEngine &diags = self->sema->getDiagnostics();
        bool wasSuppressed = diags.getSuppressAllDiagnostics();
        diags.setSuppressAllDiagnostics(true);
        QualType type;
        const Expr *expression = nullptr;
        bool good = self->parseTypeof(text, &type, &expression);
        diags.setSuppressAllDiagnostics(wasSuppressed);
        if (!good) {
            self->queryText = "the compiler does not accept it here";
            DiagnosticCollector collector;
            std::unique_ptr<DiagnosticConsumer> owned = diags.takeClient();
            DiagnosticConsumer *client = diags.getClient();
            diags.setClient(&collector, false);
            self->parseTypeof(text, &type, &expression);
            diags.setClient(client, owned.release() != nullptr);
            if (!collector.first.empty()) self->queryText = collector.first;
            out->why = self->queryText.c_str();
            return false;
        }
        ASTContext &ast = self->sema->getASTContext();
        Expr::EvalResult result;
        if (!expression->EvaluateAsRValue(result, ast, /*InConstantContext=*/true) || result.HasSideEffects) {
            self->queryText = self->notConstant(expression, ast);
            out->why = self->queryText.c_str();
            return false;
        }
        if (!readValue(result.Val, expression->getType(), out, &out->root, self->queryText, 0)) {
            out->why = self->queryText.c_str();
            return false;
        }
        return true;
    }

    // The return type of the function whose body is being parsed.
    static bool returnType(void *context, char *out, uint32_t capacity) {
        auto *self = static_cast<SmithHook *>(context);
        if (self->sema == nullptr) return false;
        const FunctionDecl *function = self->sema->getCurFunctionDecl(true);
        if (function == nullptr) return false;
        QualType type = function->getReturnType().getCanonicalType();
        snprintf(out, capacity, "%s", type.getAsString(self->sema->getASTContext().getPrintingPolicy()).c_str());
        return true;
    }

    static bool fieldOf(void *context, const char *name, uint32_t length, SmithLookupKind space, uint32_t index,
                        const char **field_name, const char **field_type) {
        auto *self = static_cast<SmithHook *>(context);
        QualType type = self->completeType(name, length, space);
        const RecordDecl *record = type.isNull() ? nullptr : type->getAsRecordDecl();
        if (record == nullptr) return false;
        uint32_t at = 0;
        for (const FieldDecl *field : record->fields()) {
            if (at++ != index) continue;
            self->fieldName = field->getName().str();
            self->fieldType = field->getType().getAsString(self->sema->getASTContext().getPrintingPolicy());
            *field_name = self->fieldName.c_str();
            *field_type = self->fieldType.c_str();
            return true;
        }
        return false;
    }

    // The named fields of the struct a spelling names, and the `index`th of them (runtime/lang_fields.c). An unnamed
    // bit-field is padding, not a member; a member that is an anonymous struct or union has no name to reach it by.
    // Whether a struct holds a run-time array (a `struct smith_array_...`) as a member, however deep: through arrays,
    // other structs and anonymous members. A worklist of records, so there is no recursion.
    static bool holdsSlice(const RecordDecl *first) {
        const RecordDecl *work[64];
        size_t count = 0;
        work[count++] = first;
        for (size_t at = 0; at < count; at++) {
            for (const FieldDecl *field : work[at]->fields()) {
                QualType member = field->getType().getCanonicalType();
                while (const ArrayType *array = member->getAsArrayTypeUnsafe())
                    member = array->getElementType().getCanonicalType();
                const RecordDecl *inner = member->getAsRecordDecl();
                if (inner == nullptr) continue;
                if (inner->getName().starts_with("smith_array_")) return true;
                if (inner->getDefinition() != nullptr && count < 64) {
                    bool seen = false;
                    for (size_t i = 0; i < count; i++)
                        seen = seen || work[i] == inner->getDefinition();
                    if (!seen) work[count++] = inner->getDefinition();
                }
            }
        }
        return false;
    }

    static bool typeField(void *context, const char *spelling, uint32_t length, uint32_t index, SmithFieldInfo *out) {
        auto *self = static_cast<SmithHook *>(context);
        *out = SmithFieldInfo{};
        if (self->sema == nullptr) return false;
        QualType type = self->readType(StringRef(spelling, length));
        if (type.isNull()) {
            out->status = SmithFields_Unreadable;
            return true;
        }
        const RecordDecl *record = type.getCanonicalType()->getAsRecordDecl();
        if (record == nullptr) {
            out->status = SmithFields_NotRecord;
            return true;
        }
        if (record->isUnion()) {
            out->status = SmithFields_Union;
            return true;
        }
        if (record->getDefinition() == nullptr) {
            out->status = SmithFields_Incomplete;
            return true;
        }
        out->has_slice = holdsSlice(record->getDefinition());
        for (const FieldDecl *field : record->getDefinition()->fields()) {
            if (field->isAnonymousStructOrUnion()) {
                out->status = SmithFields_Anonymous;
                return true;
            }
            if (field->getName().empty()) continue; // padding
            if (out->count++ != index) continue;
            snprintf(out->name, sizeof out->name, "%s", field->getName().str().c_str());
            snprintf(out->type, sizeof out->type, "%s",
                     field->getType().getAsString(self->sema->getASTContext().getPrintingPolicy()).c_str());
            out->bit_field = field->isBitField();
            out->tuple = record->getName().starts_with("smith_tuple");
        }
        return true;
    }

    static void renameName(void *context, const char *name, uint32_t length, const char *prefixed,
                           uint32_t prefixedLength) {
        auto *self = static_cast<SmithHook *>(context);
        self->renames[self->PP.getIdentifierInfo(StringRef(name, length))] =
            self->PP.getIdentifierInfo(StringRef(prefixed, prefixedLength));
    }

    // Whether the text has the word `if` or `for` and then `constexpr`, white space between (comments and strings
    // count).
    static bool hasIfConstexpr(StringRef text) {
        for (size_t at = text.find("constexpr"); at != StringRef::npos; at = text.find("constexpr", at + 9)) {
            size_t before = at;
            while (before > 0 && isWhitespace(text[before - 1]))
                before -= 1;
            bool is_if = before >= 2 && text.substr(before - 2, 2) == "if" &&
                         (before == 2 || !isAsciiIdentifierContinue(text[before - 3]));
            bool is_for = before >= 3 && text.substr(before - 3, 3) == "for" &&
                          (before == 3 || !isAsciiIdentifierContinue(text[before - 4]));
            if (is_if || is_for) return true;
        }
        return false;
    }

    // Whether the code (not its comments or strings) uses `fields(` (a reflection), or the type `Field` without
    // defining one: a name `Field` that is not the name of a typedef (`} Field;`) nor a member (runtime/lang_fields.c).
    static bool hasFieldsUse(StringRef text) {
        bool reflects = false;
        bool used = false;
        char previous = 0; // the last character of code before the word
        for (size_t at = 0; at < text.size();) {
            char c = text[at];
            if (c == '/' && at + 1 < text.size() && (text[at + 1] == '/' || text[at + 1] == '*')) {
                bool line = text[at + 1] == '/';
                size_t end = text.find(line ? "\n" : "*/", at + 2);
                at = end == StringRef::npos ? text.size() : end + (line ? 1 : 2);
            } else if (c == '"' || c == '\'') {
                size_t end = at + 1;
                while (end < text.size() && text[end] != c && text[end] != '\n')
                    end += text[end] == '\\' ? 2 : 1;
                at = end + 1;
                previous = c;
            } else if (isAsciiIdentifierStart(c)) {
                size_t end = at + 1;
                while (end < text.size() && isAsciiIdentifierContinue(text[end]))
                    end += 1;
                StringRef word = text.substr(at, end - at);
                size_t next = end;
                while (next < text.size() && isWhitespace(text[next]))
                    next += 1;
                if (word == "fields" && next < text.size() && text[next] == '(' && previous != '.' && previous != '>')
                    reflects = true;
                if (word == "Field") {
                    if (previous == '}' && next < text.size() && text[next] == ';') return false; // its own `Field`
                    used = used || (previous != '.' && previous != ':' && previous != '>');
                }
                previous = 'a';
                at = end;
            } else {
                if (!isWhitespace(c)) previous = c;
                at += 1;
            }
        }
        return reflects || used;
    }

    // Whether the code (not its comments or strings) has `type` before a name or a `*`, other than in a generic's
    // `constexpr type T`, which is inside parentheses: a type value is declared (runtime/lang_type.c).
    static bool hasTypeValue(StringRef text) {
        int paren = 0;
        StringRef previous;
        for (size_t at = 0; at < text.size();) {
            char c = text[at];
            if (c == '/' && at + 1 < text.size() && (text[at + 1] == '/' || text[at + 1] == '*')) {
                bool line = text[at + 1] == '/';
                size_t end = text.find(line ? "\n" : "*/", at + 2);
                at = end == StringRef::npos ? text.size() : end + (line ? 1 : 2);
            } else if (c == '"' || c == '\'') {
                size_t end = at + 1;
                while (end < text.size() && text[end] != c && text[end] != '\n')
                    end += text[end] == '\\' ? 2 : 1;
                at = end + 1;
            } else if (isAsciiIdentifierStart(c)) {
                size_t end = at + 1;
                while (end < text.size() && isAsciiIdentifierContinue(text[end]))
                    end += 1;
                StringRef word = text.substr(at, end - at);
                size_t next = end;
                while (next < text.size() && isWhitespace(text[next]))
                    next += 1;
                bool named = next < text.size() && (isAsciiIdentifierStart(text[next]) || text[next] == '*');
                if (word == "type" && named && !(paren > 0 && previous == "constexpr")) return true;
                previous = word;
                at = end;
            } else {
                paren += c == '(';
                paren -= c == ')' && paren > 0;
                at += 1;
            }
        }
        return false;
    }

    // How many `import` lines the text has: an import's functions over `type` are defined after the last (#107).
    static uint32_t countImportLines(StringRef text) {
        uint32_t lines = 0;
        for (size_t at = text.find("import "); at != StringRef::npos; at = text.find("import ", at + 7)) {
            if (at == 0 || text[at - 1] == '\n') lines += 1; // an import line begins its line
        }
        return lines;
    }

    static void typeValues(void *context) {
        static_cast<SmithHook *>(context)->host.maybe_type_value = true;
    }

    // Whether the text has `enum {` or `enum Name {` with a `;` at its own depth in the braces: an enum with payloads
    // (runtime/lang_enum.c; comments and strings count).
    static bool hasPayloadEnum(StringRef text) {
        for (size_t at = text.find("enum"); at != StringRef::npos; at = text.find("enum", at + 4)) {
            if ((at > 0 && isAsciiIdentifierContinue(text[at - 1])) ||
                (at + 4 < text.size() && isAsciiIdentifierContinue(text[at + 4])))
                continue;
            size_t open = at + 4;
            while (open < text.size() && isWhitespace(text[open]))
                open += 1;
            while (open < text.size() && isAsciiIdentifierContinue(text[open]))
                open += 1;
            while (open < text.size() && isWhitespace(text[open]))
                open += 1;
            if (open >= text.size() || text[open] != '{') continue;
            int depth = 0;
            for (size_t close = open; close < text.size(); close++) {
                depth += text[close] == '{';
                depth -= text[close] == '}';
                if (depth == 1 && text[close] == ';') return true;
                if (depth == 0) break;
            }
        }
        return false;
    }

    // Whether the text has `struct {` and, before the matching `}`, no `;`: a tuple type (comments and strings count).
    static bool hasTuple(StringRef text) {
        for (size_t at = text.find("struct"); at != StringRef::npos; at = text.find("struct", at + 6)) {
            if (at > 0 && isAsciiIdentifierContinue(text[at - 1])) continue;
            size_t open = at + 6;
            while (open < text.size() && isWhitespace(text[open]))
                open += 1;
            if (open >= text.size() || text[open] != '{') continue;
            int depth = 0;
            bool semicolon = false;
            size_t close = open;
            for (; close < text.size(); close++) {
                depth += text[close] == '{';
                depth -= text[close] == '}';
                semicolon = semicolon || text[close] == ';';
                if (depth == 0 || semicolon) break;
            }
            if (!semicolon && close < text.size()) return true;
        }
        return false;
    }

    // The end of the comment, string or character literal that begins at `at` (`at` itself when it is code): a `[:]` in
    // either is no slice (runtime/lang_slice.c's smith_lang_text_skip).
    static size_t skipLiteral(StringRef text, size_t at) {
        char c = text[at];
        if (c == '/' && at + 1 < text.size() && text[at + 1] == '/') {
            size_t end = text.find('\n', at);
            return end == StringRef::npos ? text.size() : end;
        }
        if (c == '/' && at + 1 < text.size() && text[at + 1] == '*') {
            size_t end = text.find("*/", at + 2);
            return end == StringRef::npos ? text.size() : end + 2;
        }
        bool separator = at > 0 && (isalnum(static_cast<unsigned char>(text[at - 1])) || text[at - 1] == '_');
        if (c != '"' && !(c == '\'' && !separator)) return at;
        for (at += 1; at < text.size() && text[at] != c && text[at] != '\n'; at++) {
            if (text[at] == '\\') at += 1;
        }
        return at < text.size() ? at + 1 : text.size();
    }

    // Whether the code of the text has the word `str`, the type `const char[:]` of a module (runtime/lang_slice.c).
    static bool hasStr(StringRef text) {
        for (size_t at = 0; at + 3 <= text.size(); at++) {
            size_t after = skipLiteral(text, at);
            if (after != at) {
                at = after - 1;
                continue;
            }
            bool before = at > 0 && (isalnum((unsigned char)text[at - 1]) || text[at - 1] == '_');
            bool follows = at + 3 < text.size() && (isalnum((unsigned char)text[at + 3]) || text[at + 3] == '_');
            if (!before && !follows && text.substr(at, 3) == "str") return true;
        }
        return false;
    }

    // Whether a `[ ]` of the text holds a `:` that is neither `::` nor the `:` of a `? :`: `T xs[:]` or `xs[a:b]`, a
    // run-time array (runtime/lang_slice.c). Comments and strings do not count.
    static bool hasSlice(StringRef text) {
        for (size_t at = 0; at < text.size(); at++) {
            size_t after = skipLiteral(text, at);
            if (after != at) {
                at = after - 1;
                continue;
            }
            if (text[at] != '[') continue;
            int depth = 0;
            int conditions = 0;
            for (size_t scan = at; scan < text.size(); scan++) {
                size_t next = skipLiteral(text, scan);
                if (next != scan) {
                    scan = next - 1;
                    continue;
                }
                char c = text[scan];
                if (c == '[' || c == '(' || c == '{') depth += 1;
                if (c == ']' || c == ')' || c == '}') depth -= 1;
                if (depth <= 0 || c == ';') break;
                if (depth != 1) continue;
                if (c == '?') conditions += 1;
                if (c != ':') continue;
                if (scan + 1 < text.size() && text[scan + 1] == ':') {
                    scan += 1;
                } else if (conditions > 0) {
                    conditions -= 1;
                } else {
                    return true;
                }
            }
        }
        return false;
    }

    // The integer a predefined macro stands for (`__FLT_MANT_DIG__`): its tokens' spellings, less parentheses.
    bool macroNumber(const char *name, int64_t *value) {
        const MacroInfo *info = PP.getMacroInfo(PP.getIdentifierInfo(name));
        if (info == nullptr) return false;
        std::string text;
        for (const Token &token : info->tokens())
            text += PP.getSpelling(token);
        text.erase(std::remove_if(text.begin(), text.end(), [](char c) { return c == '(' || c == ')'; }), text.end());
        char *end = nullptr;
        long long number = strtoll(text.c_str(), &end, 10);
        if (text.empty() || *end != 0) return false;
        *value = number;
        return true;
    }

    // The facts `target::` names (module target), from the preprocessor of this compile. The raw lowering writes the
    // `#if` of each into its output (runtime/lang_target.c), and these are the same questions.
    static bool targetFact(void *context, SmithTarget fact, int64_t *value) {
        auto *self = static_cast<SmithHook *>(context);
        Preprocessor &pp = self->PP;
        const SanitizerSet &sanitize = pp.getLangOpts().Sanitize;
        switch (fact) {
        case SmithTarget_Arch:
            *value = pp.isMacroDefined("__aarch64__") ? 1 : pp.isMacroDefined("__x86_64__") ? 2 : 0;
            return true;
        case SmithTarget_Os:
            *value =
                pp.isMacroDefined("__APPLE__") && pp.isMacroDefined("__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__") ? 1
                : pp.isMacroDefined("__linux__") ? 2
                                                 : 0;
            return true;
        case SmithTarget_AddressSanitizer:
            *value = sanitize.has(SanitizerKind::Address) ? 1 : 0;
            return true;
        case SmithTarget_UndefinedSanitizer:
            *value = sanitize.hasOneOf(SanitizerKind::Undefined) ? 1 : 0;
            return true;
        case SmithTarget_ThreadSanitizer:
            *value = sanitize.has(SanitizerKind::Thread) ? 1 : 0;
            return true;
        case SmithTarget_Assertions:
            *value = pp.isMacroDefined("NDEBUG") ? 0 : 1;
            return true;
        case SmithTarget_StaticAnalyzer: // defined only while clang-tidy's analyzer checks run
            *value = pp.isMacroDefined("__clang_analyzer__") ? 1 : 0;
            return true;
        case SmithTarget_FloatRadix:
            return self->macroNumber("__FLT_RADIX__", value);
        case SmithTarget_F32MantissaBits:
            return self->macroNumber("__FLT_MANT_DIG__", value);
        case SmithTarget_F64MantissaBits:
            return self->macroNumber("__DBL_MANT_DIG__", value);
        case SmithTarget_FloatEvalMethod: { // a builtin macro, not a defined one: asked as the builtin asks
            LangOptions::FPEvalMethodKind method = pp.getTUFPEvalMethod();
            if (method == LangOptions::FEM_UnsetOnCommandLine) method = pp.getLangOpts().getFPEvalMethod();
            if (method == LangOptions::FEM_UnsetOnCommandLine) method = pp.getTargetInfo().getFPEvalMethod();
            *value = static_cast<int>(method);
            return true;
        }
        }
        return false;
    }

    // The value of a `constexpr` integer variable or an enumeration constant the parser has parsed, as the number it
    // is: an unsigned value is read as unsigned (a `uint32_t` of all ones is 4294967295, not -1), and one that does
    // not fit an int64_t is not offered.
    static bool constantValue(void *context, const char *name, uint32_t length, int64_t *value) {
        auto *self = static_cast<SmithHook *>(context);
        const NamedDecl *decl = self->findDecl(name, length, SmithLookup_Value);
        llvm::APSInt number;
        if (auto *constant = dyn_cast_or_null<EnumConstantDecl>(decl)) {
            number = constant->getInitVal();
        } else if (auto *variable = dyn_cast_or_null<VarDecl>(decl)) {
            if (!variable->isConstexpr() || variable->isInvalidDecl()) return false;
            const APValue *evaluated = variable->evaluateValue();
            if (evaluated == nullptr || !evaluated->isInt()) return false;
            number = evaluated->getInt();
        } else {
            return false;
        }
        if (number.isUnsigned() ? number.getActiveBits() > 63 : number.getSignificantBits() > 64) return false;
        *value = number.isUnsigned() ? static_cast<int64_t>(number.getZExtValue()) : number.getSExtValue();
        return true;
    }

    static void watchName(void *context, const char *name, uint32_t length) {
        auto *self = static_cast<SmithHook *>(context);
        self->PP.getIdentifierInfo(StringRef(name, length))->setHookWatched(true);
    }

    static void watchMode(void *context, SmithWatch mode) {
        auto *self = static_cast<SmithHook *>(context);
        self->mode = mode == SmithWatch_All      ? Preprocessor::HookAll
                     : mode == SmithWatch_Braces ? Preprocessor::HookBraces
                                                 : Preprocessor::HookWatched;
        if (self->afterSized == 0) self->PP.setHookMode(self->mode); // else the look ends with it
    }

    // The token `back` places before the one offered, from the Preprocessor's record of the parser's tokens.
    static bool previousToken(void *context, unsigned back, SmithToken *token) {
        auto *self = static_cast<SmithHook *>(context);
        const IdentifierInfo *info = nullptr;
        tok::TokenKind kind = self->PP.getHookPrevious(back, info);
        if (kind == tok::unknown) return false;
        *token = SmithToken{};
        token->handle = smith_token_no_handle;
        token->kind = SmithTokenKind_Punctuator;
        token->text = "";
        if (info != nullptr) {
            token->kind = SmithTokenKind_Identifier;
            token->text = info->getNameStart();
            token->length = info->getLength();
        } else if (const char *spelling = tok::getPunctuatorSpelling(kind)) {
            token->text = spelling;
            token->length = spelling[1] == 0 ? 1 : spelling[2] == 0 ? 2 : 3;
        } else if (kind == tok::annot_module_include || kind == tok::annot_module_begin ||
                   kind == tok::annot_module_end) {
            // With -fmodules an import's header is a module import: the token that stands where it was ends a
            // declaration, as the `;` of the import did, so the next import is at the start of a declaration.
            token->text = ";";
            token->length = 1;
        } else if (const char *keyword = tok::getKeywordSpelling(kind)) {
            token->kind = SmithTokenKind_Identifier;
            token->text = keyword;
            token->length = std::strlen(keyword);
        }
        return true;
    }

    // Whether anything in the file has been declared: an import comes before the first declaration.
    static bool declaredInFile(void *context) {
        auto *self = static_cast<SmithHook *>(context);
        if (self->sema == nullptr) return false;
        for (const Decl *decl : self->sema->getASTContext().getTranslationUnitDecl()->decls()) {
            // (With -fmodules an import's header is a module import declaration at the import's own place; that is
            // the import itself, not a declaration of the file.)
            if (!decl->isImplicit() && !isa<ImportDecl>(decl) &&
                self->SM.getFileID(self->SM.getFileLoc(decl->getLocation())) == self->MainFile) {
                return true;
            }
        }
        return false;
    }

    // The import (as its header is spelled in it) that brought the file in, or null.
    const char *importOf(FileID id) {
        for (int depth = 0; depth < 64 && id.isValid(); depth++) { // include chains are short
            auto found = importSpelling.find(id.getHashValue());
            if (found != importSpelling.end()) return keep(found->second);
            id = SM.getFileID(SM.getIncludeLoc(id));
        }
        return nullptr;
    }

    // Which `import <h>;` declared the name (a declaration of that space, else a macro), for SMITH_LANG_ASK_IMPORTS:
    // the import the module wrote, never the internal header the compiler found the name in.
    static const char *declaredBy(void *context, const char *name, uint32_t length, SmithLookupKind space) {
        auto *self = static_cast<SmithHook *>(context);
        if (NamedDecl *decl = self->findDecl(name, length, space)) {
            if (const char *by = self->importOf(self->SM.getFileID(self->SM.getFileLoc(decl->getLocation())))) {
                return by;
            }
        }
        if (space == SmithLookup_Tag) return nullptr;
        IdentifierInfo *info = &self->PP.getIdentifierTable().get(StringRef(name, length));
        if (const MacroInfo *macro = self->PP.getMacroInfo(info)) {
            return self->importOf(self->SM.getFileID(macro->getDefinitionLoc()));
        }
        return nullptr;
    }

    static SmithLookup lookupName(void *context, const char *name, uint32_t length, SmithLookupKind space) {
        auto *self = static_cast<SmithHook *>(context);
        SmithLookup answer{};
        NamedDecl *decl = self->findDecl(name, length, space);
        if (decl == nullptr) return answer;
        for (NamedDecl *own : self->sizedDecls) { // a sized type's typedef is made by the hook, not the program
            if (own == decl) return answer;
        }
        answer.kind = space == SmithLookup_Tag ? SmithLookup_Tag
                      : isa<TypeDecl>(decl)    ? SmithLookup_Type
                                               : SmithLookup_Value;
        FileID where = self->SM.getFileID(self->SM.getFileLoc(decl->getLocation()));
        answer.in_main_file = where == self->MainFile;
        answer.from_import = self->importOwned(where);
        return answer;
    }

    // A module's header on the include path (smith build writes it there), read through the file manager's file system,
    // so that a relative -I means what it means to the tool that set the working directory: clangd serves many
    // projects from one process.
    std::string headerPath; // where headerOnPath found it
    bool headerOnPath(const std::string &relative) {
        for (DirectoryLookup &directory : PP.getHeaderSearchInfo().search_dir_range()) {
            if (!directory.isNormalDir()) continue;
            std::string path = directory.getName().str() + "/" + relative;
            auto buffer = PP.getFileManager().getVirtualFileSystem().getBufferForFile(path);
            if (!buffer) continue;
            headerPath = path;
            headerFile = std::move(*buffer);
            return true;
        }
        return false;
    }

    static bool readHeader(void *context, const char *relative, const char **text, uint32_t *length) {
        auto *self = static_cast<SmithHook *>(context);
        self->problem.clear();
        if (self->headerOnPath(relative)) {
            *text = self->headerFile->getBufferStart();
            *length = self->headerFile->getBufferSize();
            return true;
        }
        StringRef name(relative);
        if (!name.consume_back(".h")) return false;
        Interface *found = self->interfaceOf(name.str());
        if (found == nullptr || !found->ok) return false;
        *text = found->meta.c_str();
        *length = found->meta.size();
        return true;
    }

    FileID typesFile;
    // A place that is no file of the program: the start of a buffer of its own (issue #107).
    static uint32_t typesPlace(void *context) {
        auto *self = static_cast<SmithHook *>(context);
        if (!self->typesFile.isValid()) {
            auto buffer = llvm::MemoryBuffer::getMemBufferCopy(" ", "<smith-types>");
            self->typesFile = self->SM.createFileID(std::move(buffer), SrcMgr::C_User);
        }
        return self->SM.getLocForStartOfFile(self->typesFile).getRawEncoding();
    }

    // ---- The functions of the imports that the file runs at compile time (issue #107, runtime/lang_body.c)

    std::set<std::string> compileTimeNames;
    bool compileTimeScanned = false;

    // The names the file writes where C requires a constant: in a statement that has `static_assert`, `constexpr`,
    // `case` or `enum` (so the condition of an `if constexpr` too), and, outside a function body, in an initializer or
    // an array bound. A name is no more than that: whether it is a function of an import, the lowering asks.
    void scanCompileTime() {
        compileTimeScanned = true;
        StringRef text = SM.getBufferData(MainFile);
        Lexer lexer(SM.getLocForStartOfFile(MainFile), PP.getLangOpts(), text.begin(), text.begin(), text.end());
        std::vector<std::string> words;
        bool keyword = false;
        bool equals = false; // `=` or `[` in the statement: an initializer or an array bound
        int bodies = 0;
        int open = 0;
        bool afterParen = false;
        Token token;
        // A function's head is no place for a call at compile time (a head's words are its own names), and a function
        // body is one only where the statement says so.
        bool afterColons = false;
        int groups = 0; // parenthesized groups at the top of the statement: a head with two has a computed result type
        int parens = 0;
        auto flushWords = [&](bool head) {
            if (keyword || (bodies == 0 && (!head || groups >= 2) && (equals || open > 0 || groups >= 2)))
                compileTimeNames.insert(words.begin(), words.end());
            words.clear();
            keyword = false;
            equals = false;
            groups = 0;
        };
        for (lexer.LexFromRawLexer(token); token.isNot(tok::eof); lexer.LexFromRawLexer(token)) {
            if (token.is(tok::raw_identifier)) {
                StringRef word = token.getRawIdentifier();
                keyword = keyword || word == "static_assert" || word == "_Static_assert" || word == "constexpr" ||
                          word == "case" || word == "enum";
                if (afterColons) words.push_back(word.str()); // an import's function is written `module::name`
                afterParen = false;
                afterColons = false;
                continue;
            }
            afterColons = token.is(tok::coloncolon);
            equals = equals || token.is(tok::equal) || token.is(tok::l_square);
            parens += token.is(tok::l_paren) ? 1 : token.is(tok::r_paren) && parens > 0 ? -1 : 0;
            groups += token.is(tok::r_paren) && parens == 0 ? 1 : 0;
            if (token.is(tok::l_brace)) {
                bool body = bodies > 0 || (open == 0 && afterParen);
                flushWords(open == 0 && afterParen);
                open += 1;
                bodies += body ? 1 : 0;
                bodyKinds.push_back(body);
            } else if (token.is(tok::r_brace)) {
                flushWords(false);
                if (open > 0) {
                    open -= 1;
                    bodies -= bodyKinds.back() ? 1 : 0;
                    bodyKinds.pop_back();
                }
            } else if (token.is(tok::semi)) {
                flushWords(false);
            }
            afterParen = token.is(tok::r_paren);
        }
        flushWords(false);
        bodyKinds.clear();
    }
    std::vector<bool> bodyKinds;

    static bool mentionsName(void *context, const char *name, uint32_t length) {
        auto *self = static_cast<SmithHook *>(context);
        if (!self->compileTimeScanned) self->scanCompileTime();
        bool hit = self->compileTimeNames.count(std::string(name, length)) != 0;
        return hit;
    }

    static void includeText(void *context, const char *text, uint32_t length) {
        auto *self = static_cast<SmithHook *>(context);
        self->closeTokens();
        Action action;
        action.include = true;
        action.literal = true;
        action.body.assign(text, length);
        action.at = self->pool.empty() ? SourceLocation() : self->pool.front().getLocation();
        self->actions.push_back(std::move(action));
    }

    struct BodyFile {
        bool ok = false;
        std::string text;
        std::string path; // the file to include by (empty: made in memory)
    };
    std::map<std::string, BodyFile> bodyFiles; // by header (a/b.h)

    // The arguments of a compile of another module: the flags the driver passed, which decide where its imports are
    // found and what it declares.
    std::vector<std::string> bodyFlags() {
        std::vector<std::string> flags;
        const char *env = std::getenv("SMITH_LANG_FLAGS");
        SmallVector<StringRef, 16> parts;
        StringRef(env != nullptr ? env : "").split(parts, '\n', -1, false);
        for (StringRef part : parts)
            flags.push_back(part.str());
        return flags;
    }

    // Runs `smith cc -fsyntax-only` on the module's source with SMITH_LANG_BODY set; the body file is written to
    // `out` when it passes.
    bool makeBody(const std::string &source, const std::string &out) {
        // smith itself: the driver says where it is, since this process may be a clone of it that is called `clang`
        const char *named = std::getenv("SMITH_LANG_SELF");
        std::string self =
            named != nullptr
                ? named
                : llvm::sys::fs::getMainExecutable("smith", reinterpret_cast<void *>(&SmithHook::readBody));
        std::vector<std::string> keep = {self, "cc", "-fsyntax-only", "-w"};
        for (std::string &flag : bodyFlags())
            if (!flag.empty()) keep.push_back(flag);
        keep.push_back(source);
        std::vector<StringRef> args(keep.begin(), keep.end());
        std::vector<std::string> environment;
        for (char **entry = environ; entry != nullptr && *entry != nullptr; entry++)
            if (StringRef(*entry).starts_with("SMITH_LANG_BODY=") == false) environment.push_back(*entry);
        environment.push_back("SMITH_LANG_BODY=" + out);
        std::vector<StringRef> env(environment.begin(), environment.end());
        std::optional<StringRef> redirects[] = {std::nullopt, StringRef("/dev/null"), StringRef("/dev/null")};
        int status = llvm::sys::ExecuteAndWait(self, args, env, redirects);

        return status == 0 && llvm::sys::fs::exists(out);
    }

    static bool readBody(void *context, const char *header, const char **text, uint32_t *length, const char **path) {
        auto *self = static_cast<SmithHook *>(context);
        auto known = self->bodyFiles.find(header);
        if (known == self->bodyFiles.end()) known = self->bodyFiles.emplace(header, self->findBody(header)).first;
        if (!known->second.ok) return false;
        *text = known->second.text.c_str();
        *length = static_cast<uint32_t>(known->second.text.size());
        *path = known->second.path.empty() ? nullptr : known->second.path.c_str();
        return true;
    }

    // The source a header was generated from: the second line says (`// Generated by smith from <file>. Do not edit.`).
    std::string sourceOfHeader(StringRef contents) {
        StringRef line = contents.split('\n').second.split('\n').first;
        if (!line.consume_front("// Generated by smith from ") || !line.consume_back(". Do not edit.")) return "";
        std::string source = line.str();
        if (llvm::sys::fs::exists(source)) return source;
        std::string inside = ".smith/" + source; // a library: the build keeps its source there
        return llvm::sys::fs::exists(inside) ? inside : "";
    }

    BodyFile findBody(const std::string &header) {
        BodyFile found;
        std::string module = header.substr(0, header.size() - 2); // a/b
        std::string source;
        std::string file;
        bool onDisk = false;
        if (headerOnPath(header)) {
            file = headerPath.substr(0, headerPath.size() - 2) + ".body";
            source = sourceOfHeader(headerFile->getBuffer());
            onDisk = true;
        } else if (Interface *entry = interfaceOf(module); entry != nullptr && entry->ok) {
            source = entry->source;
        } else {
            return found;
        }
        llvm::sys::fs::file_status bodyStatus, sourceStatus;
        bool have = onDisk && !llvm::sys::fs::status(file, bodyStatus);
        bool stale = have && !source.empty() && !llvm::sys::fs::status(source, sourceStatus) &&
                     bodyStatus.getLastModificationTime() < sourceStatus.getLastModificationTime();
        if (inEditor && !have) return found; // the editor reads what a build wrote, and starts no compile
        if ((!have || stale) && !inEditor) {
            if (source.empty()) return found;
            std::string out = file + ".tmp" + std::to_string(::getpid());
            if (!onDisk) {
                llvm::SmallString<128> unique;
                if (llvm::sys::fs::createTemporaryFile("smith-body", "body", unique)) return found;
                out = unique.str().str();
            }
            bool made = makeBody(source, out);
            auto buffer = made ? llvm::MemoryBuffer::getFile(out)
                               : llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>>(std::error_code());
            bool kept =
                made && onDisk && !llvm::sys::fs::rename(out, file); // a folder that cannot be written: in memory
            if (!kept) {
                std::error_code removed = llvm::sys::fs::remove(out);
                (void)removed;
            }
            if (!buffer) return found;
            found.text = (*buffer)->getBuffer().str();
            found.path = kept ? file : "";
            found.ok = true;
            return found;
        }
        auto buffer = llvm::MemoryBuffer::getFile(file);
        if (!buffer) return found;
        found.text = (*buffer)->getBuffer().str();
        found.path = file;
        found.ok = true;
        return found;
    }

    static const char *readProblem(void *context) {
        auto *self = static_cast<SmithHook *>(context);
        return self->problem.empty() ? nullptr : self->problem.c_str();
    }

    // ---- A module found by its source
    //
    // When `import a::b` finds no a/b.h on the include path, the module's source is looked for: among the .smith files
    // of the command (SMITH_LANG_INPUTS, which smith cc sets), then at a/b.smith under the root of the importing file's
    // module. Its interface, what `smith cc --smith-header` would write, is made in memory and served as the header;
    // nothing is written. The text is given to the file manager as the contents of the source file, so the include that
    // follows reads it, and the depfile names the source.
    struct Interface {
        bool done = false;
        bool ok = false;
        bool inProgress = false;
        std::string source;   // the path found
        std::string include;  // the absolute path to include it by
        std::string text;     // the interface (on error, the message)
        std::string includes; // the lines to include before it
        std::string meta;     // the comment lines of its header (smith_lang_interface)
        std::string content;  // the module's source
    };
    std::map<std::string, Interface> interfaces; // by module path (a/b)
    std::vector<std::string> roots; // where a module's path starts: the main file's, and those of the modules found
    bool rootsMade = false;
    std::string problem;

    static std::string moduleFromPath(std::string path) {
        for (size_t at; (at = path.find('/')) != std::string::npos;)
            path.replace(at, 1, "::");
        return path;
    }

    static std::string modulePath(std::string name) {
        for (size_t at; (at = name.find("::")) != std::string::npos;)
            name.replace(at, 2, "/");
        return name;
    }

    // The folder a module path is relative to: for module a::b in x/a/b.smith, x; for a file with no module line, its
    // folder.
    std::string rootOf(const std::string &file, const std::string &module) {
        std::string tail = module + ".smith";
        if (StringRef(file).ends_with(tail)) {
            StringRef root = StringRef(file).drop_back(tail.size());
            return root.rtrim('/').str();
        }
        return llvm::sys::path::parent_path(file).str();
    }

    void addRoot(const std::string &root) {
        for (const std::string &known : roots) {
            if (known == root) return;
        }
        roots.push_back(root);
    }

    void makeRoots() {
        if (rootsMade) return;
        rootsMade = true;
        if (const char *module = smith_lang_module_name(lang)) {
            addRoot(rootOf(mainName, modulePath(module)));
        } else {
            addRoot(llvm::sys::path::parent_path(mainName).str());
        }
    }

    std::vector<std::string> candidates(const std::string &module) {
        std::vector<std::string> list;
        std::string tail = module + ".smith";
        if (const char *inputs = getenv("SMITH_LANG_INPUTS")) {
            StringRef rest(inputs);
            while (!rest.empty()) {
                auto [one, more] = rest.split('\n');
                rest = more;
                if (one != mainName && (one == tail || one.ends_with("/" + tail))) list.push_back(one.str());
            }
        }
        makeRoots();
        for (const std::string &root : roots)
            list.push_back(root.empty() ? tail : root + "/" + tail);
        return list;
    }

    std::string absolute(const std::string &path) {
        llvm::SmallString<256> full(path);
        PP.getFileManager().makeAbsolutePath(full);
        return std::string(full);
    }

    // The interface of the module at a/b, found by its source, or null (with `problem` set to why).
    Interface *interfaceOf(const std::string &module) {
        Interface &entry = interfaces[module];
        if (entry.inProgress) return nullptr;
        if (entry.done) {
            if (!entry.ok && problem.empty()) problem = entry.text;
            return &entry;
        }
        entry.done = true;
        std::string name = moduleFromPath(module);
        std::string tried;
        std::unique_ptr<llvm::MemoryBuffer> source;
        for (const std::string &path : candidates(module)) {
            auto buffer = PP.getFileManager().getVirtualFileSystem().getBufferForFile(path);
            if (!buffer) {
                tried += (tried.empty() ? "`" : ", `") + path + "`";
                continue;
            }
            entry.source = path;
            source = std::move(*buffer);
            break;
        }
        if (!source) {
            entry.text = "cannot find module `" + name + "`: no `" + module + ".h` in the include paths, no `" +
                         module + ".smith` among the input files, and nothing at the path the name implies (" + tried +
                         ")";
            problem = entry.text;
            return &entry;
        }
        entry.inProgress = true;
        make(entry, name, *source);
        entry.inProgress = false;
        return &entry;
    }

    void make(Interface &entry, const std::string &name, const llvm::MemoryBuffer &source) {
        std::string out(smith_lang_interface_capacity(), '\0');
        std::string includes(1 << 16, '\0');
        std::string meta(1 << 16, '\0');
        char error[512];
        SmithInterface result{out.data(),  out.size(),  0, includes.data(), includes.size(), 0,
                              meta.data(), meta.size(), 0};
        if (!smith_lang_interface(entry.source.c_str(), source.getBufferStart(), source.getBufferSize(), &result, error,
                                  sizeof error)) {
            entry.text = "module `" + name + "` cannot be imported: " + entry.source + ":" + error;
            problem = entry.text;
            return;
        }
        out.resize(result.text_used);
        includes.resize(result.includes_used);
        meta.resize(result.meta_used);
        entry.include = absolute(entry.source);
        entry.text = out;
        entry.includes = includes;
        entry.meta = meta;
        entry.content = source.getBuffer().str();
        entry.ok = true;
        addRoot(rootOf(entry.source, modulePath(name)));
        nestedImports(entry);
        if (entry.include != absolute(mainName)) { // never the contents of the file being compiled
            if (auto file = PP.getFileManager().getFileRef(entry.include)) {
                SM.overrideFileContents(*file, llvm::MemoryBuffer::getMemBufferCopy(entry.text, entry.include));
            }
        }
    }

    // What the interface includes of other modules is the source of those, when they too are found by their source.
    void nestedImports(Interface &entry) {
        std::string rewritten;
        SmallVector<StringRef, 16> lines;
        StringRef(entry.includes).split(lines, '\n', -1, false);
        for (StringRef line : lines) {
            StringRef path = line;
            if (path.consume_front("#include \"") && path.consume_back(".h\"")) {
                std::string module = path.str();
                Interface *nested = headerOnPath(module + ".h") ? nullptr : interfaceOf(module);
                if (nested != nullptr && nested->ok) {
                    rewritten += "#include \"" + nested->include + "\"\n";
                    continue;
                }
            }
            rewritten += line.str() + "\n";
        }
        entry.includes = rewritten;
        problem.clear();
    }

    // The path to include a module by, when its interface came from its source, and the lines to include first.
    bool includePathOf(const char *header, std::string &path, std::string &preface) {
        StringRef name(header);
        if (!name.consume_back(".h")) return false;
        auto found = interfaces.find(name.str());
        if (found == interfaces.end() || !found->second.ok || headerOnPath(header)) return false;
        path = found->second.include;
        preface = found->second.includes;
        return true;
    }

    // The uses of a module that could not be imported are kept out of the diagnostics.
    static void suppressUse(void *context, uint32_t location) {
        static_cast<SmithHook *>(context)->link->suppressed.insert(location);
    }

    // (Not in the editor: clangd shows a note in the file it is in as a diagnostic of its own, beside the error.)
    static void hintAt(void *context, uint32_t location, const char *message) {
        if (!inEditor) static_cast<SmithHook *>(context)->link->hints[location] = message;
    }

    // ---- The names as written
    //
    // The diagnostics, and what a language server shows (shim/names.h), name things as the source does. The table is
    // the lowering's, while this hook has the lowering, and a copy of it after.
    std::shared_ptr<PresenterLink> link = std::make_shared<PresenterLink>();
    std::shared_ptr<smith::FileNames> snapshot;

    void installPresenter() {
        DiagnosticsEngine &diagnostics = PP.getDiagnostics();
        DiagnosticConsumer *inner = diagnostics.getClient();
        if (inner == nullptr) return;
        std::unique_ptr<DiagnosticConsumer> owned = diagnostics.takeClient();
        diagnostics.setClient(new Presenter(std::move(owned), inner, link), /*ShouldOwnClient=*/true);
        Presenter::presentToolEngine(link);
    }

    static void copyName(const SmithWrittenName &from, smith::WrittenName &to) {
        to.lowered.assign(from.lowered, from.lowered_length);
        to.written.assign(from.written, from.written_length);
        to.member.assign(from.member, from.member_length);
        to.module.assign(from.module, from.module_length);
        to.direct = from.direct;
    }

    bool writtenName(StringRef lowered, smith::WrittenName &out) override {
        if (holding) {
            SmithWrittenName found;
            if (!smith_lang_written_find(lang, lowered.data(), lowered.size(), &found)) return false;
            copyName(found, out);
            return true;
        }
        const smith::WrittenName *found = snapshot ? snapshot->find(lowered) : nullptr;
        if (found == nullptr) return false;
        out = *found;
        return true;
    }

    // Takes the table before the lowering is given back, and tells the language server of it.
    void keepNames() {
        if (!holding) return;
        auto names = std::make_shared<smith::FileNames>();
        if (const char *module = smith_lang_module_name(lang)) names->module = module;
        for (size_t i = 0, count = smith_lang_written_count(lang); i < count; i++) {
            SmithWrittenName found;
            if (!smith_lang_written_at(lang, i, &found)) break;
            smith::WrittenName name;
            copyName(found, name);
            names->index[name.lowered] = names->names.size();
            names->names.push_back(std::move(name));
        }
        for (size_t i = 0, count = smith_lang_instance_count(lang); i < count; i++) {
            SmithInstanceOrigin origin;
            if (!smith_lang_instance_at(lang, i, &origin)) break;
            names->instance_files[origin.name] = {absolute(origin.source), origin.exported};
        }
        for (size_t i = 0, count = smith_lang_call_site_count(lang); i < count; i++) {
            SmithCallSite found;
            if (!smith_lang_call_site_at(lang, i, &found)) break;
            smith::CallSite site;
            SourceLocation at = SourceLocation::getFromRawEncoding(found.location);
            if (!at.isFileID()) continue;
            site.line = SM.getSpellingLineNumber(at) - 1;
            site.column = SM.getSpellingColumnNumber(at) - 1;
            site.length = found.length;
            site.hover = found.hover;
            if (found.definition_location != UINT32_MAX) {
                SourceLocation def = SourceLocation::getFromRawEncoding(found.definition_location);
                site.file = mainName;
                site.definition_line = SM.getSpellingLineNumber(def) - 1;
                site.definition_column = SM.getSpellingColumnNumber(def) - 1;
            } else {
                site.file = absolute(found.file);
                site.definition_line = found.line > 0 ? found.line - 1 : 0;
                site.definition_column = found.column > 0 ? found.column - 1 : 0;
            }
            names->calls.push_back(std::move(site));
        }
        snapshot = names;
        // A preamble is built from the start of the file, up to its first line that is no directive: a module's
        // `module` line is the first, so what it parses is a stub of the file, whose names are no names of the file.
        if (mainName.empty() || SM.getBufferData(MainFile).empty() || PP.getPreprocessorOpts().GeneratePreamble) return;
        if (reachedEnd) {
            smith::publishNames(mainName, names);
        } else {
            keepQualifier(); // a completion: the parse stopped at the cursor
        }
    }

    // Where the cursor stood, when it came right after `x::`.
    void keepQualifier() {
        smith::Qualifier qualifier;
        SmithQualifier found;
        if (smith_lang_qualifier(lang, &found)) {
            SourceLocation at = SourceLocation::getFromRawEncoding(found.location);
            qualifier.present = true;
            qualifier.binding = found.binding;
            qualifier.prefix = found.prefix;
            qualifier.module = found.module;
            qualifier.partial.assign(found.partial, found.partial_length);
            qualifier.is_c = found.is_c;
            qualifier.line = SM.getSpellingLineNumber(at) - 1;
            qualifier.column = SM.getSpellingColumnNumber(at) - 1;
        }
        smith::publishQualifier(mainName, qualifier);
    }
    bool reachedEnd = false;
    IntrusiveRefCntPtr<ArrayEnd> arrayEnd;

  public:
    // The translation unit is complete (called through Sema's external source).
    void completed() {
        if (!moduleFile || sema == nullptr || checked) return;
        checked = true;
        ArrayParameters(sema->Context, SM, [this](SourceLocation at) { return generatedAt(at); }).run();
    }

  private:
    bool checked = false;
    bool moduleFile = false; // the file has a `module` line: its array parameters keep their shape

    // The lowering goes back to the next file; what it knew of this one stays.
    void giveBack() {
        keepNames();
        Lowering::release(holding);
    }
};

void ArrayEnd::ReadTentativeDefinitions(SmallVectorImpl<VarDecl *> &) {
    hook->completed();
}

#ifdef CLANG_HAS_DEBUG_NAME_HOOK
// The debugger shows a .smith declaration as the source writes it, with its whole module name, the same in every
// compile unit (patches/clang-debug-name.patch asks for each function, variable, typedef and tag). A type is `Box` in
// the namespaces `geometry` and `box`, so that lldb prints `geometry::box::Box` and parses it in an expression. A
// function or a variable is named `geometry::box::area` at the top, because lldb shows a C function by its DW_AT_name
// only, and a namespace would leave it `area`. A name the lowering did not make, and everything in a file that is no
// .smith file, keeps clang's.
bool debugName(const NamedDecl *decl, std::string &scope, std::string &name) {
    NameSource *hook = SmithHook::debugOwner;
    if (hook == nullptr || !decl->getDeclContext()->getRedeclContext()->isTranslationUnit()) return false;
    bool object = isa<FunctionDecl>(decl) || isa<VarDecl>(decl);
    if (!object && !isa<TypedefNameDecl>(decl) && !isa<TagDecl>(decl)) return false;
    const IdentifierInfo *identifier = decl->getIdentifier();
    smith::WrittenName written;
    if (identifier != nullptr && identifier->getName() == "smith_user_main") { // `i32 main(io::Io *io)`, as written
        scope = "";
        name = "main";
        return true;
    }
    if (identifier == nullptr || !hook->writtenName(identifier->getName(), written) || written.module.empty())
        return false;
    scope = object ? "" : written.module;
    name = object ? written.module + "::" + written.member : written.member;
    return true;
}
#endif

std::unique_ptr<TokenHook> makeHook(Preprocessor &pp) {
    SourceManager &sm = pp.getSourceManager();
    OptionalFileEntryRef entry = sm.getFileEntryRefForID(sm.getMainFileID());
    if (!entry || !entry->getName().ends_with(".smith")) {
        Presenter::presentToolEngine(nullptr); // what the last .smith file taught the tool's consumer is not this one's
        return nullptr;
    }
    // A .smith file runs C function bodies in clang's constant evaluator where C requires a constant
    // (patches/clang-constexpr-c.patch). The option is Benign, so it is also on under clang modules (-fmodules).
    const_cast<LangOptions &>(pp.getLangOpts()).SmithConstexprC = true;
    return std::make_unique<SmithHook>(pp);
}

} // namespace

// This process is the editor's (clangd).
extern "C" void smith_hook_editor() {
    inEditor = true;
}

// Makes every translation unit in this process offer its .smith main file to the lowering.
extern "C" void smith_hook_install() {
    Preprocessor::TokenHookFactory = makeHook;
#ifdef CLANG_HAS_DEBUG_NAME_HOOK
    clang::CodeGen::DebugNameHook = debugName;
#endif
}

void Presenter::presentToolEngine(const std::shared_ptr<PresenterLink> &link) {
    if (toolEngine == nullptr) return;
    DiagnosticConsumer *client = toolEngine->getClient();
    if (client == nullptr) return;
    std::shared_ptr<PresenterLink> use = link ? link : std::make_shared<PresenterLink>();
    if (client == toolConsumer) {
        static_cast<Presenter *>(client)->relink(use);
        return;
    }
    if (link == nullptr) return; // nothing to show yet
    auto *presenter = new Presenter(nullptr, client, use);
    presenter->tool = true;
    toolConsumer = presenter;
    toolEngine->setClient(presenter, /*ShouldOwnClient=*/true);
}

namespace smith {

void presentThrough(DiagnosticsEngine &engine) {
    toolEngine = &engine;
}

namespace {
std::mutex registryLock;
std::map<std::string, std::shared_ptr<const FileNames>> registry;
} // namespace

void publishNames(const std::string &path, std::shared_ptr<const FileNames> names) {
    std::lock_guard<std::mutex> guard(registryLock);
    registry[path] = std::move(names);
}

std::shared_ptr<const FileNames> namesOf(const std::string &path) {
    std::lock_guard<std::mutex> guard(registryLock);
    auto found = registry.find(path);
    return found == registry.end() ? nullptr : found->second;
}

namespace {
std::map<std::string, Qualifier> qualifiers;
}

void publishQualifier(const std::string &path, const Qualifier &qualifier) {
    std::lock_guard<std::mutex> guard(registryLock);
    qualifiers[path] = qualifier;
}

Qualifier qualifierOf(const std::string &path) {
    std::lock_guard<std::mutex> guard(registryLock);
    auto found = qualifiers.find(path);
    return found == qualifiers.end() ? Qualifier() : found->second;
}
} // namespace smith
