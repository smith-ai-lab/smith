// The clang side of .smith files: the hook that gives the lowering (runtime/lang.c) the tokens of a .smith file, after
// macro expansion and before the parser, puts back what it emits, and answers its questions from the parser's tables.
//
// The hook itself is a patch to clang (patches/clang-token-hook.patch): clang::TokenHook, offered each token that
// leaves Preprocessor::Lex. Everything here is smith's. It stays small and C++ only because clang's API is: the logic
// is in C, in the lowering.
#include "names.h"

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
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/Sema.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/VirtualFileSystem.h"

extern "C" {
#include "../runtime/lang.h"
}

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

using namespace clang;

namespace {

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
        if (link->source == nullptr || !present(level, info, at)) inner->HandleDiagnostic(level, info);
    }

  private:
    std::unique_ptr<DiagnosticConsumer> owned;
    DiagnosticConsumer *inner;
    std::shared_ptr<PresenterLink> link;
    bool dropNotes = false;
    std::vector<std::pair<SourceLocation, std::string>> pendingNotes; // of the error just passed on
    const DiagnosticsEngine *engine = nullptr;

    // The notes that name the instances an error is in, after the error and its own notes.
    void flushInstanceNotes() {
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
        host.expr_type = &SmithHook::exprType;
        host.instance = &SmithHook::instanceBegin;
        host.locate = &SmithHook::locatePlace;
        host.field = &SmithHook::fieldOf;
        host.read_header = &SmithHook::readHeader;
        host.read_problem = &SmithHook::readProblem;
        host.suppress = &SmithHook::suppressUse;
        host.hint = &SmithHook::hintAt;
        if (OptionalFileEntryRef entry = SM.getFileEntryRefForID(MainFile)) {
            mainName = entry->getName().str();
        }
        host.main_file = mainName.c_str();
        host.maybe_main = SM.getBufferData(MainFile).contains("main");
        host.maybe_generic =
            SM.getBufferData(MainFile).contains("type ") || SM.getBufferData(MainFile).contains("auto ");
        host.maybe_if_constexpr = hasIfConstexpr(SM.getBufferData(MainFile));
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
        if (!stats.on) return feed(tok);
        auto begin = std::chrono::steady_clock::now();
        bool consumed = feed(tok);
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
        if (before == tok::period || before == tok::arrow || before == tok::kw_struct || before == tok::kw_union ||
            before == tok::kw_enum) {
            return Fast::Kept;
        }
        if (declaredByProgram(info)) return Fast::No; // the program's own u32, perhaps
        if (mayEndType(before)) return Fast::No;
        if (!body && smith_lang_io_import(lang) && info->getName() == "i32") return Fast::No; // `i32 main(io::Io *io)`
        ensureSized(sized->second, tok.getLocation());
        return Fast::Kept;
    }

    // The declarations of a module, as they stream by: the lowering decides each token without looking ahead, or says
    // it must (Slow).
    Fast tryFast(Token &tok) {
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
        FileID id =
            SM.createFileID(llvm::MemoryBuffer::getMemBufferCopy(text, "<smith-copy>"), SrcMgr::C_User, 0, 0, first);
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
        std::string text = action.preface + std::string("#include ") + (action.angled ? "<" : "\"") + action.header +
                           (action.angled ? ">" : "\"") + "\n";
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
    // as the tokens the call wrote, lowered (`const geometry_Point *`, `__UINT32_TYPE__`), so a small reader of C's
    // type names is enough: builtin type words, the sized types' own names, a typedef or a `struct`, `union` or `enum`
    // tag, qualifiers, and pointers. Anything else (arrays, function pointers) is not read, and the lowering then
    // identifies the type by its spelling.
    std::string queryText;

    // The builtin type the words name, or a null type.
    QualType builtinType(const std::vector<StringRef> &words) {
        ASTContext &ast = sema->getASTContext();
        unsigned longs = 0, shorts = 0, chars = 0, ints = 0, floats = 0, doubles = 0, voids = 0, bools = 0, halves = 0,
                 int128s = 0;
        bool isSigned = false, isUnsigned = false;
        for (StringRef word : words) {
            if (word == "long")
                longs += 1;
            else if (word == "short")
                shorts += 1;
            else if (word == "char")
                chars += 1;
            else if (word == "int")
                ints += 1;
            else if (word == "float")
                floats += 1;
            else if (word == "double")
                doubles += 1;
            else if (word == "void")
                voids += 1;
            else if (word == "_Bool" || word == "bool")
                bools += 1;
            else if (word == "_Float16")
                halves += 1;
            else if (word == "__int128")
                int128s += 1;
            else if (word == "signed")
                isSigned = true;
            else if (word == "unsigned")
                isUnsigned = true;
            else
                return QualType();
        }
        if (voids == 1) return ast.VoidTy;
        if (bools == 1) return ast.BoolTy;
        if (floats == 1) return ast.FloatTy;
        if (halves == 1) return ast.Float16Ty;
        if (doubles == 1) return longs == 1 ? ast.LongDoubleTy : ast.DoubleTy;
        if (int128s == 1) return isUnsigned ? ast.UnsignedInt128Ty : ast.Int128Ty;
        if (chars == 1) return isUnsigned ? ast.UnsignedCharTy : isSigned ? ast.SignedCharTy : ast.CharTy;
        if (shorts == 1) return isUnsigned ? ast.UnsignedShortTy : ast.ShortTy;
        if (longs == 2) return isUnsigned ? ast.UnsignedLongLongTy : ast.LongLongTy;
        if (longs == 1) return isUnsigned ? ast.UnsignedLongTy : ast.LongTy;
        if (ints == 1 || isSigned || isUnsigned) return isUnsigned ? ast.UnsignedIntTy : ast.IntTy;
        return QualType();
    }

    // The type a name stands for: one of the sized types' own names (`__UINT32_TYPE__`), a typedef, or a tag after its
    // keyword.
    QualType namedType(StringRef name, StringRef tag) {
        if (tag.empty()) {
            for (const SizedName &entry : sizedTable) {
                if (entry.macro != nullptr && name == entry.macro) return sizedType(entry.macro);
            }
        }
        SmithLookupKind space = tag.empty() ? SmithLookup_Type : SmithLookup_Tag;
        auto *decl = dyn_cast_or_null<TypeDecl>(findDecl(name.data(), name.size(), space));
        return decl == nullptr ? QualType() : sema->getASTContext().getTypeDeclType(decl);
    }

    // The words of a spelling: names, and `*` as a word of its own.
    static std::vector<StringRef> typeWords(StringRef spelling) {
        std::vector<StringRef> words;
        for (size_t at = 0; at < spelling.size();) {
            char c = spelling[at];
            if (c == ' ') {
                at += 1;
            } else if (c == '*') {
                words.push_back(spelling.substr(at, 1));
                at += 1;
            } else if (isAsciiIdentifierContinue(c)) {
                size_t end = at;
                while (end < spelling.size() && isAsciiIdentifierContinue(spelling[end]))
                    end += 1;
                words.push_back(spelling.substr(at, end - at));
                at = end;
            } else {
                return {};
            }
        }
        return words;
    }

    QualType readType(StringRef spelling) {
        std::vector<StringRef> words = typeWords(spelling);
        if (words.empty()) return QualType();
        size_t at = 0;
        Qualifiers quals;
        std::vector<StringRef> builtin;
        QualType base;
        for (; at < words.size() && words[at] != "*"; at++) {
            StringRef word = words[at];
            if (word == "const")
                quals.addConst();
            else if (word == "volatile")
                quals.addVolatile();
            else if (word == "restrict")
                quals.addRestrict();
            else if (word == "struct" || word == "union" || word == "enum") {
                if (at + 1 >= words.size() || !base.isNull()) return QualType();
                base = namedType(words[++at], word);
            } else if (base.isNull() && builtin.empty() && sizedOrTypedef(word)) {
                base = namedType(word, StringRef());
            } else {
                builtin.push_back(word);
            }
        }
        if (base.isNull())
            base = builtinType(builtin);
        else if (!builtin.empty())
            return QualType();
        if (base.isNull()) return QualType();
        base = sema->getASTContext().getQualifiedType(base, quals);
        for (; at < words.size(); at++) { // pointers, and the qualifiers after each
            if (words[at] == "*") {
                base = sema->getASTContext().getPointerType(base);
                continue;
            }
            Qualifiers after;
            if (words[at] == "const")
                after.addConst();
            else if (words[at] == "volatile")
                after.addVolatile();
            else if (words[at] == "restrict")
                after.addRestrict();
            else
                return QualType();
            base = sema->getASTContext().getQualifiedType(base, after);
        }
        return base;
    }

    // Whether a word is a name (not a builtin type word or qualifier): a typedef, or a sized type's own name.
    bool sizedOrTypedef(StringRef word) {
        static const char *const builtinWords[] = {"signed", "unsigned", "short",   "long", "int",
                                                   "char",   "float",    "double",  "void", "_Bool",
                                                   "bool",   "_Float16", "__int128"};
        for (const char *builtin : builtinWords) {
            if (word == builtin) return false;
        }
        return true;
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
        out->is_bool = canonical->isBooleanType();
        out->is_enum = canonical->isEnumeralType();
        out->is_integer = canonical->isIntegerType() && !out->is_bool && !out->is_enum;
        out->is_float = canonical->isRealFloatingType();
        out->is_signed = out->is_float || canonical->isSignedIntegerOrEnumerationType();
        out->is_pointer = canonical->isPointerType();
        out->is_record = canonical->isRecordType();
        out->complete = !canonical->isIncompleteType() && !canonical->isDependentType() && !canonical->isVoidType() &&
                        !canonical->isFunctionType();
        if (out->complete) {
            out->size = ast.getTypeSizeInChars(canonical).getQuantity();
            out->alignment = ast.getTypeAlignInChars(canonical).getQuantity();
        }
        return true;
    }

    // ---- The type of an expression
    //
    // `test::check_equal(a, b)` leaves T to be told from its arguments. The arguments are the lowered tokens of a name,
    // a field chain, `*e`, `&e`, `e[i]`, a call, a cast or a literal: what a person writes to name a value. Anything
    // else (arithmetic, a conditional) is not typed, and the call must say its type. The scope is the parser's, so a
    // local declared by an earlier statement is found.
    std::string typeWhy;

    static size_t closingOf(const std::vector<std::string> &tokens, size_t open, size_t end) {
        int depth = 0;
        for (size_t at = open; at < end; at++) {
            const std::string &token = tokens[at];
            if (token == "(" || token == "[" || token == "{") depth += 1;
            if (token == ")" || token == "]" || token == "}") depth -= 1;
            if (depth == 0) return at;
        }
        return end;
    }

    QualType literalType(const std::string &token) {
        ASTContext &ast = sema->getASTContext();
        if (token[0] == '"') return ast.getPointerType(ast.CharTy);
        if (token[0] == '\'') return ast.IntTy;
        if (token == "true" || token == "false") return ast.BoolTy;
        bool hex = token.size() > 1 && token[0] == '0' && (token[1] == 'x' || token[1] == 'X');
        bool real = !hex && token.find_first_of(".eE") != std::string::npos;
        char last = static_cast<char>(std::tolower(static_cast<unsigned char>(token.back())));
        if (real) return last == 'f' ? ast.FloatTy : ast.DoubleTy;
        bool isUnsigned = token.find_first_of("uU") != std::string::npos;
        size_t longs =
            static_cast<size_t>(std::count_if(token.begin(), token.end(), [](char c) { return c == 'l' || c == 'L'; }));
        if (longs >= 2) return isUnsigned ? ast.UnsignedLongLongTy : ast.LongLongTy;
        if (longs == 1) return isUnsigned ? ast.UnsignedLongTy : ast.LongTy;
        return isUnsigned ? ast.UnsignedIntTy : ast.IntTy;
    }

    // The type of the name (a variable, a parameter, a function): null with typeWhy when it is not found.
    QualType nameType(const std::string &name) {
        auto *decl = dyn_cast_or_null<ValueDecl>(findDecl(name.data(), name.size(), SmithLookup_Value));
        if (decl == nullptr) {
            typeWhy = "`" + name + "` is not a name the compiler knows here";
            return QualType();
        }
        return decl->getType();
    }

    // The lanes of an ext-vector, `v.x` or `v.yx`: the element type for one lane, else a vector of that many.
    QualType laneType(const ExtVectorType *vector, const std::string &name) {
        static const StringRef sets[] = {"xyzw", "rgba"};
        bool known = false;
        for (StringRef set : sets) {
            known = !name.empty();
            for (char c : name)
                known = known && set.contains(c);
            if (known) break;
        }
        if (!known) { // s0, hi, lo, even, odd and the like: the compiler's own names for lanes
            typeWhy = "no lanes named `" + name + "`";
            return QualType();
        }
        if (name.size() == 1) return vector->getElementType();
        ASTContext &ast = sema->getASTContext();
        QualType lanes = ast.getExtVectorType(vector->getElementType(), static_cast<unsigned>(name.size()));
        // A vector type has no spelling a type argument can be written with (`__attribute__((ext_vector_type(2)))`
        // does not read back as a type), so the vector is named by a typedef the program has for it.
        QualType canonical = lanes.getCanonicalType();
        for (const Decl *decl : ast.getTranslationUnitDecl()->decls()) {
            const auto *typedefName = dyn_cast<TypedefNameDecl>(decl);
            if (typedefName != nullptr && typedefName->getUnderlyingType().getCanonicalType() == canonical &&
                !typedefName->getName().starts_with("__")) {
                return ast.getTypedefType(ElaboratedTypeKeyword::None, /*Qualifier=*/std::nullopt, typedefName);
            }
        }
        typeWhy = "no typedef names the vector of `." + name + "`; write the type as the argument";
        return QualType();
    }

    QualType memberType(QualType base, bool arrow, const std::string &name) {
        if (arrow) {
            if (!base->isPointerType()) {
                typeWhy = "`->` needs a pointer";
                return QualType();
            }
            base = base->getPointeeType();
        }
        if (const auto *vector = base->getAs<ExtVectorType>()) return laneType(vector, name);
        const RecordDecl *record = base->getAsRecordDecl();
        if (record == nullptr) {
            typeWhy = "`." + name + "` needs a struct, a union or a vector";
            return QualType();
        }
        if (const RecordDecl *definition = record->getDefinition()) {
            for (const NamedDecl *member :
                 definition->lookup(&PP.getIdentifierTable().get(name))) { // anonymous members too
                if (const auto *field = dyn_cast<ValueDecl>(member)) return field->getType();
            }
        }
        typeWhy = "no field `" + name + "`";
        return QualType();
    }

    QualType elementType(QualType type) {
        if (type->isPointerType()) return type->getPointeeType();
        if (const auto *array = sema->getASTContext().getAsArrayType(type)) return array->getElementType();
        typeWhy = "that is not a pointer or an array";
        return QualType();
    }

    QualType resultOfCall(QualType callee) {
        if (callee->isPointerType()) callee = callee->getPointeeType();
        if (const auto *function = callee->getAs<FunctionType>()) return function->getReturnType();
        typeWhy = "that is not a function";
        return QualType();
    }

    // The primary expression at the start of [begin, end): its type, and where it ends in `next`.
    QualType primaryType(const std::vector<std::string> &t, size_t begin, size_t end, size_t *next) {
        const std::string &token = t[begin];
        *next = begin + 1;
        bool word = std::isalpha(static_cast<unsigned char>(token[0])) || token[0] == '_';
        if (!word) {
            if (std::isdigit(static_cast<unsigned char>(token[0])) || token[0] == '"' || token[0] == '\'')
                return literalType(token);
            typeWhy = "`" + token + "` does not begin a name or a literal";
            return QualType();
        }
        if (token == "true" || token == "false") return literalType(token);
        if (token == "nullptr") return sema->getASTContext().NullPtrTy;
        if (token ==
            "__extension__") { // how the lowering writes a call to a generic: its type is the declared result's
            size_t open = begin + 1;
            size_t name = open + 3;
            while (open + 2 < end && t[open] == "(" && t[open + 1] == "{" && t[open + 2] == "extern" && name < end &&
                   t[name] != "(") {
                name += 1; // to the `(` of the declaration's parameters
            }
            std::string spelling;
            for (size_t at = open + 3; open + 3 < name && at + 1 < name && name < end; at++)
                spelling += (at > open + 3 ? " " : "") + t[at];
            QualType result = spelling.empty() ? QualType() : readType(spelling);
            if (result.isNull()) {
                typeWhy = "a call to a generic function is not typed";
                return QualType();
            }
            *next = closingOf(t, open, end) + 1;
            return result;
        }
        if (token == "sizeof" || token == "alignof" || token == "_Alignof" || token == "_Countof") {
            *next = begin + 1 < end && t[begin + 1] == "(" ? closingOf(t, begin + 1, end) + 1 : begin + 2;
            return sema->getASTContext().getCanonicalType(sema->getASTContext().getSizeType());
        }
        QualType type = nameType(token);
        if (type.isNull() || begin + 1 >= end || t[begin + 1] != "(") return type;
        size_t close = closingOf(t, begin + 1, end);
        *next = close + 1;
        return resultOfCall(type);
    }

    // One step after a primary expression: `.f`, `->f`, `[i]` or a call.
    QualType postfixType(const std::vector<std::string> &t, size_t *at, size_t end, QualType type) {
        const std::string &token = t[*at];
        if ((token == "." || token == "->") && *at + 1 < end) {
            QualType member = memberType(type, token == "->", t[*at + 1]);
            *at += 2;
            return member;
        }
        if (token == "[" || token == "(") {
            *at = closingOf(t, *at, end) + 1;
            return token == "[" ? elementType(type) : resultOfCall(type);
        }
        typeWhy = "`" + token + "` is not understood in this expression";
        return QualType();
    }

    QualType prefixType(const std::string &op, QualType type) {
        ASTContext &ast = sema->getASTContext();
        if (op == "*") return elementType(type);
        if (op == "&") return ast.getPointerType(type);
        if (op == "!") return ast.IntTy;
        return type; // - + ~
    }

    // Binary operators, by precedence (higher binds tighter); 0 for a token that is no binary operator.
    static int binaryPrecedence(const std::string &op) {
        if (op == "||") return 1;
        if (op == "&&") return 2;
        if (op == "|") return 3;
        if (op == "^") return 4;
        if (op == "&") return 5;
        if (op == "==" || op == "!=") return 6;
        if (op == "<" || op == "<=" || op == ">" || op == ">=") return 7;
        if (op == "<<" || op == ">>") return 8;
        if (op == "+" || op == "-") return 9;
        if (op == "*" || op == "/" || op == "%") return 10;
        return 0;
    }

    // Whether the token ends an operand: a name or a literal, `)` or `]`.
    static bool endsOperand(const std::string &token) {
        if (token == ")" || token == "]") return true;
        unsigned char first = static_cast<unsigned char>(token[0]);
        return std::isalnum(first) || first == '_' || first == '"' || first == '\'';
    }

    // Whether the `)` at `close` ends a cast, `(i32)`, whose operand follows, rather than an operand.
    bool endsCast(const std::vector<std::string> &t, size_t begin, size_t close) {
        int depth = 0;
        size_t open = close;
        for (;; open--) {
            if (t[open] == ")" || t[open] == "]" || t[open] == "}") depth += 1;
            if (t[open] == "(" || t[open] == "[" || t[open] == "{") depth -= 1;
            if (depth == 0 || open == begin) break;
        }
        if (depth != 0 || t[open] != "(" || (open > begin && endsOperand(t[open - 1]))) return false;
        std::string spelling;
        for (size_t at = open + 1; at < close; at++)
            spelling += (at > open + 1 ? " " : "") + t[at];
        return !readType(spelling).isNull();
    }

    // The index of the binary operator that ends the expression, the last of the lowest precedence outside any
    // brackets (they associate to the left), or `end` for none. A `?` is found first: the conditional is below them
    // all.
    size_t splitBinary(const std::vector<std::string> &t, size_t begin, size_t end, bool *conditional) {
        size_t found = end;
        int lowest = 100;
        int depth = 0;
        *conditional = false;
        for (size_t at = end; at > begin; at--) {
            const std::string &token = t[at - 1];
            if (token == ")" || token == "]" || token == "}") depth += 1;
            if (token == "(" || token == "[" || token == "{") depth -= 1;
            if (depth != 0) continue;
            if (token == "?") {
                found = at - 1; // the first `?` from the left is found last: the scan goes back
                *conditional = true;
                lowest = 0;
                continue;
            }
            int precedence = binaryPrecedence(token);
            if (precedence == 0 || precedence >= lowest || at - 1 == begin || !endsOperand(t[at - 2])) continue;
            if (t[at - 2] == ")" && endsCast(t, begin, at - 2)) continue;
            found = at - 1;
            lowest = precedence;
        }
        return found;
    }

    // The type both operands of an arithmetic operator are converted to (C's usual arithmetic conversions).
    QualType usualArithmetic(QualType a, QualType b) {
        ASTContext &ast = sema->getASTContext();
        if (a->isVectorType() || b->isVectorType()) return a->isVectorType() ? a : b; // a vector and its scalars
        if (a->isFloatingType() || b->isFloatingType()) {
            if (!a->isFloatingType()) return b;
            if (!b->isFloatingType()) return a;
            return ast.getFloatingTypeOrder(a, b) >= 0 ? a : b;
        }
        if (!a->isIntegralOrEnumerationType() || !b->isIntegralOrEnumerationType()) {
            typeWhy = "an operator needs numbers";
            return QualType();
        }
        if (ast.isPromotableIntegerType(a)) a = ast.getPromotedIntegerType(a);
        if (ast.isPromotableIntegerType(b)) b = ast.getPromotedIntegerType(b);
        a = a.getUnqualifiedType();
        b = b.getUnqualifiedType();
        if (ast.hasSameType(a, b)) return a;
        bool signedA = a->isSignedIntegerType();
        bool signedB = b->isSignedIntegerType();
        if (signedA == signedB) return ast.getIntegerTypeOrder(a, b) >= 0 ? a : b;
        QualType unsignedType = signedA ? b : a;
        QualType signedType = signedA ? a : b;
        if (ast.getIntegerTypeOrder(unsignedType, signedType) >= 0) return unsignedType;
        if (ast.getIntWidth(signedType) > ast.getIntWidth(unsignedType)) return signedType;
        return ast.getCorrespondingUnsignedType(signedType);
    }

    // The type of `left op right`.
    QualType binaryType(const std::string &op, QualType left, QualType right) {
        ASTContext &ast = sema->getASTContext();
        int precedence = binaryPrecedence(op);
        if (precedence <= 2 || precedence == 6 || precedence == 7) return ast.IntTy; // && || == != < <= > >=
        if (op == "<<" || op == ">>") {
            if (!left->isIntegralOrEnumerationType()) {
                typeWhy = "a shift needs an integer";
                return QualType();
            }
            return ast.isPromotableIntegerType(left) ? ast.getPromotedIntegerType(left) : left.getUnqualifiedType();
        }
        if ((op == "+" || op == "-") && (left->isPointerType() || right->isPointerType())) {
            if (left->isPointerType() && right->isPointerType()) return ast.getCanonicalType(ast.getPointerDiffType());
            return left->isPointerType() ? left : right;
        }
        return usualArithmetic(left, right);
    }

    QualType subexpressionType(const std::vector<std::string> &t, size_t begin, size_t end) {
        return expressionType(
            std::vector<std::string>(t.begin() + static_cast<long>(begin), t.begin() + static_cast<long>(end)));
    }

    // The type of an expression with a binary operator or a `?:` at its top, or null with typeWhy (and `handled`
    // false when there is none).
    QualType operatorType(const std::vector<std::string> &t, size_t begin, size_t end, bool *handled) {
        bool conditional = false;
        size_t at = splitBinary(t, begin, end, &conditional);
        *handled = at < end;
        if (!*handled) return QualType();
        if (conditional) { // a ? b : c has the type b and c share
            int depth = 0;
            size_t colon = end;
            for (size_t i = at + 1; i < end && colon == end; i++) {
                if (t[i] == "(" || t[i] == "[" || t[i] == "{" || t[i] == "?") depth += 1;
                if (t[i] == ")" || t[i] == "]" || t[i] == "}") depth -= 1;
                if (t[i] == ":" && depth == 0) colon = i;
                if (t[i] == ":" && depth > 0) depth -= 1;
            }
            if (colon == end) {
                typeWhy = "a `?` has no `:`";
                return QualType();
            }
            QualType yes = subexpressionType(t, at + 1, colon);
            QualType no = yes.isNull() ? yes : subexpressionType(t, colon + 1, end);
            if (yes.isNull() || no.isNull()) return QualType();
            if (yes->isArithmeticType() && no->isArithmeticType()) return usualArithmetic(yes, no);
            return yes;
        }
        QualType left = subexpressionType(t, begin, at);
        QualType right = left.isNull() ? left : subexpressionType(t, at + 1, end);
        if (left.isNull() || right.isNull()) return QualType();
        return binaryType(t[at], left, right);
    }

    // The type of the expression the tokens spell, unqualified, with arrays and functions as pointers.
    QualType expressionType(std::vector<std::string> t) {
        size_t begin = 0;
        size_t end = t.size();
        for (size_t guard = 0;
             guard < end && end - begin >= 2 && t[begin] == "(" && closingOf(t, begin, end) == end - 1; guard++) {
            begin += 1; // the parentheses around it all
            end -= 1;
        }
        if (begin >= end) {
            typeWhy = "the expression is empty";
            return QualType();
        }
        bool handled = false;
        QualType operated = operatorType(t, begin, end, &handled);
        if (handled) return operated.isNull() ? operated : finishType(operated);
        if (t[begin] == "(") { // a cast: its type is the type
            size_t close = closingOf(t, begin, end);
            std::string spelling;
            for (size_t at = begin + 1; at < close; at++)
                spelling += (at > begin + 1 ? " " : "") + t[at];
            QualType cast = close + 1 < end ? readType(spelling) : QualType();
            if (!cast.isNull()) return finishType(cast);
        }
        std::vector<std::string> prefix;
        while (begin < end && (t[begin] == "*" || t[begin] == "&" || t[begin] == "-" || t[begin] == "+" ||
                               t[begin] == "!" || t[begin] == "~")) {
            prefix.push_back(t[begin++]);
        }
        if (begin >= end) {
            typeWhy = "an operator has no operand";
            return QualType();
        }
        size_t at = 0;
        QualType type = primaryType(t, begin, end, &at);
        for (size_t guard = 0; !type.isNull() && at < end && guard < end; guard++)
            type = postfixType(t, &at, end, type);
        for (size_t i = prefix.size(); !type.isNull() && i > 0; i--)
            type = prefixType(prefix[i - 1], type);
        return type.isNull() ? type : finishType(type);
    }

    QualType finishType(QualType type) {
        ASTContext &ast = sema->getASTContext();
        if (type->isArrayType()) return ast.getPointerType(elementType(type));
        if (type->isFunctionType()) return ast.getPointerType(type);
        return type.getUnqualifiedType();
    }

    static bool exprType(void *context, const char *tokens, uint32_t length, uint32_t derefs, SmithExprType *out) {
        auto *self = static_cast<SmithHook *>(context);
        *out = SmithExprType{};
        if (self->sema == nullptr) return false;
        self->typeWhy.clear();
        std::vector<std::string> parts;
        std::string current;
        for (uint32_t at = 0; at < length; at++) {
            if (tokens[at] == '\x1f') {
                parts.push_back(current);
                current.clear();
            } else {
                current += tokens[at];
            }
        }
        if (!current.empty()) parts.push_back(current);
        QualType type = parts.empty() ? QualType() : self->expressionType(parts);
        for (uint32_t i = 0; !type.isNull() && i < derefs; i++)
            type = self->elementType(type);
        if (!type.isNull()) type = self->finishType(type);
        if (type.isNull()) {
            self->queryText = self->typeWhy.empty() ? "that expression's type is not understood" : self->typeWhy;
            out->why = self->queryText.c_str();
            return false;
        }
        const PrintingPolicy &policy = self->sema->getASTContext().getPrintingPolicy();
        snprintf(out->written, sizeof out->written, "%s", type.getAsString(policy).c_str());
        snprintf(out->canonical, sizeof out->canonical, "%s", type.getCanonicalType().getAsString(policy).c_str());
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

    static void renameName(void *context, const char *name, uint32_t length, const char *prefixed,
                           uint32_t prefixedLength) {
        auto *self = static_cast<SmithHook *>(context);
        self->renames[self->PP.getIdentifierInfo(StringRef(name, length))] =
            self->PP.getIdentifierInfo(StringRef(prefixed, prefixedLength));
    }

    // Whether the text has the word `if` and then `constexpr`, white space between (comments and strings count).
    static bool hasIfConstexpr(StringRef text) {
        for (size_t at = text.find("constexpr"); at != StringRef::npos; at = text.find("constexpr", at + 9)) {
            size_t before = at;
            while (before > 0 && isWhitespace(text[before - 1]))
                before -= 1;
            if (before >= 2 && text.substr(before - 2, 2) == "if" &&
                (before == 2 || !isAsciiIdentifierContinue(text[before - 3]))) {
                return true;
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

    // The value of a `constexpr` integer variable the parser has parsed, for an `if constexpr` condition.
    static bool constantValue(void *context, const char *name, uint32_t length, int64_t *value) {
        auto *self = static_cast<SmithHook *>(context);
        auto *variable = dyn_cast_or_null<VarDecl>(self->findDecl(name, length, SmithLookup_Value));
        if (variable == nullptr || !variable->isConstexpr() || variable->isInvalidDecl()) return false;
        const APValue *evaluated = variable->evaluateValue();
        if (evaluated == nullptr || !evaluated->isInt()) return false;
        *value = evaluated->getInt().getSExtValue();
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
    bool headerOnPath(const std::string &relative) {
        for (DirectoryLookup &directory : PP.getHeaderSearchInfo().search_dir_range()) {
            if (!directory.isNormalDir()) continue;
            std::string path = directory.getName().str() + "/" + relative;
            auto buffer = PP.getFileManager().getVirtualFileSystem().getBufferForFile(path);
            if (!buffer) continue;
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

    // The lowering goes back to the next file; what it knew of this one stays.
    void giveBack() {
        keepNames();
        Lowering::release(holding);
    }
};

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
