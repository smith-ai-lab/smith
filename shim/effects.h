// smith-effects: a clang-tidy check that a program touches the world only through capabilities passed down from main.
// Registered by shim/tidy.cpp (smith lint, and smith lsp's built-in checks); the rules are in the smith-io skill.
//
//   1. A raw effect is a call into a system header that is not on the pure allow list below (or an Objective-C message
//      to a system framework's class or object). It is reported unless the function that makes it is in a
//      capability's implementation.
//   2. Failure is not an effect: abort, assert, __builtin_trap, __builtin_unreachable and smith's failure path.
//   3. A capability is a record marked [[clang::annotate("smith::capability")]]. Its implementation is the module
//      that defines it: for .smith the module file, for C the source file beside the header that defines it (io.c
//      with io.h; lib/io.c or src/io.c with include/.../io.h).
//   4. An exported function of a capability's implementation that reaches a raw effect, directly or through the file's
//      own static functions, must take or return a capability.
//   5. A function marked [[clang::annotate("smith::main_only")]] (smith_io_make) may be called only in main.
//   6. No capability, or pointer to one, in static storage.
// A record that holds a capability (a field of one, or of one that holds one) is a capability for rules 4 and 6, so a
// project's bundle of capabilities passes the signature test, but it does not make the module its implementation.
#pragma once

#include "clang-tidy/ClangTidyCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

#include "names.h"

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace clang::tidy::smith_effects {

// ---- What is pure, in one place ------------------------------------------------------------------------------------
//
// An allow list: a call into a system header is an effect unless it is here. A deny list of effects would never be
// complete.

// Headers whose every function is pure: it computes from its arguments and memory the caller passed, and reads or
// writes nothing else. (macOS's SDK puts some of them behind `secure/_string.h` and `_ctype.h`, glibc behind `bits/`.)
constexpr std::string_view pure_headers[] = {
    "string.h",    // memcpy, memmove, memset, memcmp, strlen, strcmp...: bytes in memory
    "_string.h",   // the SDK's checked versions of the same (secure/_string.h)
    "strings.h",   // bzero, bcopy, ffs: the same
    "math.h",      // sqrt, sin, pow...: arithmetic (errno is a result, not a world)
    "mathcalls.h", // glibc declares math.h's functions here (bits/mathcalls.h), and in the next two
    "mathcalls-narrow.h",
    "mathcalls-helper-functions.h",
    "string_fortified.h", // glibc's checked versions of string.h's (bits/string_fortified.h)
    "ctype.h",            // isalpha, toupper...: a character's class
    "_ctype.h",           // the SDK's implementation of ctype.h
    "stdint.h",           // integer types and constants
    "stddef.h",           // offsetof and sizes
    "float.h",            // constants
    "limits.h",           // constants
    "stdbit.h",           // bit counts and rotations
    "stdckdint.h",        // checked integer arithmetic
    "inttypes.h",         // strtoimax, imaxabs, imaxdiv: arithmetic and parsing of a string in memory
    "tgmath.h",           // math.h's functions by type
    "complex.h",          // arithmetic
    "stdarg.h",           // reading the arguments of a variadic function
    "stdbool.h",          // constants
    "stdalign.h",         // constants
    "stdnoreturn.h",
    "iso646.h",
};

// Functions of other headers that are pure.
constexpr std::string_view pure_names[] = {
    "snprintf",                      // format into the caller's buffer
    "vsnprintf",                     // the same, from a va_list
    "sscanf",                        // parse the caller's string
    "vsscanf",                       // the same, from a va_list
    "atoi",                          // parse the caller's string
    "atol",                          // the same
    "atoll",                         // the same
    "atof",                          // the same
    "qsort",                         // sorts the caller's array
    "qsort_r",                       // the same, with a context
    "bsearch",                       // searches the caller's array
    "abs",                           // arithmetic
    "labs",                          // arithmetic
    "llabs",                         // arithmetic
    "div",                           // arithmetic
    "ldiv",                          // arithmetic
    "lldiv",                         // arithmetic
    "__asan_poison_memory_region",   // sanitizer annotations: shadow memory bookkeeping, not the world
    "__asan_unpoison_memory_region", // the same
    "__error",                       // errno's location (macOS): a thread's last error, a result and not a world
    "__errno_location",              // the same (glibc)
    "difftime",                      // subtracts two time_t values
};

// The strto* family parses the caller's string: strtol, strtoll, strtoul, strtoull, strtod, strtof, strtold...
inline bool is_strto(llvm::StringRef name) {
    return name.starts_with("strto");
}

// The __builtin_* functions are pure (they compute, or tell the compiler something) except these, which are the
// library's functions of the world under a builtin's name. Named without the prefix.
constexpr std::string_view world_builtins[] = {
    "printf", "fprintf",     "vprintf",      "vfprintf",     "puts",         "putchar",     "putc",        "fputc",
    "fputs",  "fwrite",      "malloc",       "calloc",       "realloc",      "free",        "exit",        "_exit",
    "os_log", "os_log_info", "os_log_error", "os_log_debug", "os_log_fault", "dump_struct", "clear_cache", "syscall",
};

// ---- Failure -------------------------------------------------------------------------------------------------------
//
// Failure is not an effect: a program that cannot go on stops, and says why. These are allowed everywhere, and the
// functions of this list may themselves write the report.
constexpr std::string_view failure_names[] = {
    "abort",          "__assert_rtn",          "__assert_fail",       "__assert",        "_assert",
    "__builtin_trap", "__builtin_unreachable", "__builtin_debugtrap", "__builtin_abort", "__builtin_verbose_trap",
};
constexpr std::string_view failure_prefixes[] = {
    "smith_failure", "smith_contract_", "smith_requires",  "smith_ensures",           "smith_invariant",
    "smith_assert",  "smith_check",     "smith_test_fail", "smith_test_expect_abort",
};

// ---- What to take instead ------------------------------------------------------------------------------------------
struct Replacement {
    std::string_view names; // space separated
    const char *capability;
    const char *how;
};
constexpr Replacement replacements[] = {
    {"fopen fclose fread fseek fseeko ftell ftello rewind open openat close read write pread pwrite lseek stat lstat "
     "fstat access unlink unlinkat remove rename mkdir rmdir opendir readdir closedir fsync ftruncate mmap munmap "
     "chdir "
     "getcwd realpath mkstemp mkdtemp tmpfile fcntl ioctl chmod chown utimes symlink readlink link flock truncate "
     "fdopen freopen fileno getdelim getline",
     "SmithFiles", "read and write files through it (smith_files_read, smith_files_write)"},
    {"printf puts putchar putc fputs fputc fprintf vprintf vfprintf perror fgets getchar getc fgetc scanf fscanf "
     "vscanf vfscanf fflush setvbuf ungetc dprintf vdprintf isatty ttyname",
     "SmithConsole", "print and read through it (smith_console_print, smith_console_read_line)"},
    {"time clock clock_gettime clock_gettime_nsec_np gettimeofday sleep usleep nanosleep mach_absolute_time "
     "mach_timebase_info mach_continuous_time localtime localtime_r gmtime gmtime_r mktime strftime ctime asctime",
     "SmithClock", "read the time through it (smith_clock_now, smith_clock_wall, smith_clock_sleep)"},
    {"fork vfork execl execle execlp execv execve execvp execvP posix_spawn posix_spawnp system popen pclose wait "
     "waitpid wait4 kill killpg signal sigaction raise pipe dup dup2 getpid getppid setsid setpgid getpgid "
     "sigprocmask sigaltstack",
     "SmithProcesses", "start and wait for processes through it (smith_processes_run, smith_processes_spawn)"},
    {"socket connect bind listen accept send recv sendto recvfrom sendmsg recvmsg shutdown getaddrinfo freeaddrinfo "
     "gethostbyname setsockopt getsockopt getsockname getpeername select poll",
     "SmithNet", "connect through it (smith_net_connect, smith_net_send, smith_net_receive)"},
    {"kqueue kevent kevent64 epoll_wait", "SmithIo", "wait through its loop (smith_io_run)"},
    {"malloc calloc realloc reallocf free aligned_alloc posix_memalign valloc memalign", "SmithArena",
     "allocate from it (smith_arena_push, or arena::push in .smith)"},
    {"rand srand rand_r random srandom drand48 srand48 lrand48 mrand48 erand48 arc4random arc4random_uniform "
     "arc4random_buf getentropy getrandom",
     "SmithKey", "derive randomness from it (smith_key_derive, then smith_uniform_*_sample)"},
    {"getenv setenv putenv unsetenv environ", "SmithArgs", "read the environment through it (smith_args_env)"},
    {"exit _exit atexit quick_exit _Exit", nullptr, "return the status from main instead"},
};

inline const Replacement *replacement_for(llvm::StringRef name) {
    for (const Replacement &entry : replacements) {
        std::string_view all = entry.names;
        size_t at = 0;
        while (at < all.size()) {
            size_t end = all.find(' ', at);
            if (end == std::string_view::npos) end = all.size();
            if (all.substr(at, end - at) == std::string_view(name.data(), name.size())) return &entry;
            at = end + 1;
        }
    }
    return nullptr;
}

template <size_t N> bool in_list(const std::string_view (&list)[N], llvm::StringRef name) {
    for (std::string_view entry : list)
        if (entry == std::string_view(name.data(), name.size())) return true;
    return false;
}

inline bool is_failure_name(llvm::StringRef name) {
    if (in_list(failure_names, name)) return true;
    for (std::string_view prefix : failure_prefixes)
        if (name.starts_with(llvm::StringRef(prefix.data(), prefix.size()))) return true;
    return false;
}

enum class Verdict { Pure, Failure, Raw, Other };

inline std::string base_name(llvm::StringRef path) {
    return llvm::sys::path::filename(path).str();
}

// `__builtin_memcpy` and `__builtin___memcpy_chk` are `memcpy`.
inline llvm::StringRef bare_builtin(llvm::StringRef name) {
    if (name.consume_front("__builtin___")) {
        name.consume_back("_chk");
        return name;
    }
    name.consume_front("__builtin_");
    return name;
}

// What a function is to the rules, from its declarations: a function the project wrote is `Other` (its own body is
// judged where it is written); a system function is pure, a failure, or a raw effect.
inline Verdict classify(const FunctionDecl *fn, const SourceManager &sm) {
    llvm::StringRef name = fn->getName();
    if (name.empty()) return Verdict::Other;
    if (is_failure_name(name)) return Verdict::Failure;
    if (name.starts_with("__builtin_")) {
        if (is_failure_name(name)) return Verdict::Failure;
        return in_list(world_builtins, bare_builtin(name)) ? Verdict::Raw : Verdict::Pure;
    }
    if (name.starts_with("__sync_") || name.starts_with("__atomic_") || name.starts_with("__c11_atomic"))
        return Verdict::Pure;
    bool system = false;
    bool pure_header = false;
    for (const FunctionDecl *redeclaration : fn->redecls()) {
        SourceLocation at = redeclaration->getLocation();
        if (at.isInvalid()) continue;
        if (!sm.isInSystemHeader(at) || sm.getBufferName(at) == "<smith-bodies>") continue; // a copy for compile time
        system = true;
        llvm::StringRef file = sm.getFilename(sm.getExpansionLoc(at));
        if (file.empty()) file = sm.getPresumedLoc(at).getFilename();
        std::string base = base_name(file);
        for (std::string_view header : pure_headers)
            if (header == base) pure_header = true;
    }
    if (!system && fn->getBuiltinID() != 0 && fn->getLocation().isInvalid()) system = true; // an implicit declaration
    if (!system) return Verdict::Other;
    if (pure_header || in_list(pure_names, name) || is_strto(name)) return Verdict::Pure;
    return Verdict::Raw;
}

// ---- Capabilities --------------------------------------------------------------------------------------------------

inline bool has_annotation(const Decl *decl, llvm::StringRef text) {
    for (const auto *attribute : decl->specific_attrs<AnnotateAttr>())
        if (attribute->getAnnotation() == text) return true;
    return false;
}

inline bool function_has_annotation(const FunctionDecl *fn, llvm::StringRef text) {
    for (const FunctionDecl *redeclaration : fn->redecls())
        if (has_annotation(redeclaration, text)) return true;
    return false;
}

inline const RecordDecl *record_of(QualType type) {
    for (int i = 0; i < 8; i++) {
        type = type.getCanonicalType();
        if (type->isPointerType()) {
            type = type->getPointeeType();
        } else if (type->isArrayType()) {
            type = cast<ArrayType>(type.getTypePtr())->getElementType();
        } else {
            break;
        }
    }
    const RecordDecl *record = type->getAsRecordDecl();
    if (record == nullptr) return nullptr;
    const RecordDecl *definition = record->getDefinition();
    return definition != nullptr ? definition : record;
}

inline bool is_marked(const RecordDecl *record) {
    if (record == nullptr) return false;
    for (const TagDecl *redeclaration : record->redecls())
        if (has_annotation(redeclaration, "smith::capability")) return true;
    return false;
}

class Effects final : public ClangTidyCheck {
  public:
    Effects(llvm::StringRef Name, ClangTidyContext *Context)
        : ClangTidyCheck(Name, Context), c_files(Options.get("CFiles", true)) {}

    void storeOptions(ClangTidyOptions::OptionMap &Opts) override {
        Options.store(Opts, "CFiles", c_files);
    }

    void registerMatchers(ast_matchers::MatchFinder *Finder) override {
        using namespace ast_matchers;
        Finder->addMatcher(functionDecl(isDefinition(), unless(isImplicit())).bind("function"), this);
        Finder->addMatcher(varDecl(hasGlobalStorage(), unless(isImplicit())).bind("variable"), this);
    }

    void check(const ast_matchers::MatchFinder::MatchResult &Result) override {
        const SourceManager &sm = *Result.SourceManager;
        if (!c_files && !in_smith_file(sm)) return;
        if (const auto *fn = Result.Nodes.getNodeAs<FunctionDecl>("function")) {
            if (!sm.isInSystemHeader(fn->getLocation())) function(fn, Result);
        } else if (const auto *var = Result.Nodes.getNodeAs<VarDecl>("variable")) {
            if (!sm.isInSystemHeader(var->getLocation())) storage(var);
        }
    }

    void onEndOfTranslationUnit() override {
        finish();
        capability_functions.clear();
        capability_files.clear();
        scanned = false;
        reported.clear();
    }

  private:
    struct Effect {
        SourceLocation at;
        std::string callee;
        const Replacement *replacement;
        bool objc = false;
    };
    struct Info {
        const FunctionDecl *fn;
        std::vector<Effect> effects;
        std::vector<const FunctionDecl *> callees;
    };

    bool c_files;
    bool scanned = false;
    std::set<std::string> capability_files; // where the marked records of this unit are defined
    std::map<const FunctionDecl *, Info> capability_functions;
    std::set<unsigned> reported;
    std::map<const RecordDecl *, int> holds_cache;

    static bool in_smith_file(const SourceManager &sm) {
        OptionalFileEntryRef main = sm.getFileEntryRefForID(sm.getMainFileID());
        return main && main->getName().ends_with(".smith");
    }

    static std::string absolute(llvm::StringRef path) {
        llvm::SmallString<256> full(path);
        llvm::sys::fs::make_absolute(full);
        llvm::sys::path::remove_dots(full, true);
        return std::string(full);
    }

    static std::string file_of(const SourceManager &sm, SourceLocation at) {
        SourceLocation spelling = sm.getFileLoc(at);
        OptionalFileEntryRef entry = sm.getFileEntryRefForID(sm.getFileID(spelling));
        return entry ? absolute(entry->getName()) : std::string();
    }

    // Does a capability hold: marked, or having a field that holds one.
    bool holds(const RecordDecl *record, int depth = 0) {
        if (record == nullptr) return false;
        if (is_marked(record)) return true;
        auto cached = holds_cache.find(record);
        if (cached != holds_cache.end()) return cached->second == 1;
        holds_cache[record] = 0; // while it is being decided, a cycle does not hold one
        bool result = false;
        if (depth < 6 && record->isCompleteDefinition()) {
            for (const FieldDecl *field : record->fields()) {
                if (holds(record_of(field->getType()), depth + 1)) {
                    result = true;
                    break;
                }
            }
        }
        holds_cache[record] = result ? 1 : 0;
        return result;
    }

    bool holds(QualType type) {
        return holds(record_of(type));
    }

    void scan(ASTContext &context, const SourceManager &sm) {
        if (scanned) return;
        scanned = true;
        for (const Decl *decl : context.getTranslationUnitDecl()->decls()) {
            const auto *record = dyn_cast<RecordDecl>(decl);
            if (record == nullptr || !record->isCompleteDefinition() || !is_marked(record)) continue;
            if (sm.isInSystemHeader(record->getLocation())) continue;
            capability_files.insert(file_of(sm, record->getLocation()));
        }
    }

    static bool is_header(llvm::StringRef path) {
        llvm::StringRef extension = llvm::sys::path::extension(path);
        return extension == ".h" || extension == ".hpp" || extension == ".hh";
    }

    // The file a function belongs to. An instance of a generic is compiled in the file that calls it, but it is the
    // code of its generic's module: that module's file.
    // `private_instance` is set for an instance of a generic that its module does not export: no export either.
    std::string home_of(const FunctionDecl *fn, const SourceManager &sm, bool &private_instance) const {
        private_instance = false;
        std::string file = file_of(sm, fn->getLocation());
        if (!in_smith_file(sm)) return file;
        OptionalFileEntryRef main = sm.getFileEntryRefForID(sm.getMainFileID());
        std::shared_ptr<const ::smith::FileNames> names = ::smith::namesOf(main->getName().str());
        if (names == nullptr) return file;
        auto found = names->instance_files.find(fn->getName().str());
        if (found == names->instance_files.end()) return file;
        private_instance = !found->second.exported;
        return found->second.file;
    }

    // Whether a .smith module's file is the source of a generated header that defines a capability: the header
    // `<root>/include/smith/io.h` is made from `smith/io.smith` somewhere else in the tree.
    static bool generated_from(llvm::StringRef header, llvm::StringRef source) {
        size_t include = header.rfind("/include/");
        if (include == llvm::StringRef::npos || !header.ends_with(".h") || !source.ends_with(".smith")) return false;
        std::string tail = "/" + header.drop_front(include + 9).drop_back(2).str() + ".smith";
        return source.ends_with(tail);
    }

    // The file of a function implements a capability when it defines the type itself (a .smith module), or is the
    // source beside the header that does: same stem, in the same folder or in lib/ or src/ with the header under the
    // include/ beside them.
    bool implements(const std::string &file) const {
        llvm::StringRef stem = llvm::sys::path::stem(file);
        llvm::StringRef folder = llvm::sys::path::parent_path(file);
        for (const std::string &defined : capability_files) {
            if (defined == file || generated_from(defined, file)) return true;
            if (is_header(file) || !is_header(defined) || llvm::sys::path::stem(defined) != stem) continue;
            llvm::StringRef beside = llvm::sys::path::parent_path(defined);
            if (beside == folder) return true;
            llvm::StringRef name = llvm::sys::path::filename(folder);
            if (name != "lib" && name != "src") continue;
            std::string include = (llvm::sys::path::parent_path(folder) + "/include/").str();
            if (beside.str() == include.substr(0, include.size() - 1) ||
                llvm::StringRef(beside.str() + "/").starts_with(include))
                return true;
        }
        return false;
    }

    // The name of a callee as the source wrote it: `c::fopen` in a .smith file, `io::make` for a module's export.
    std::string written(const FunctionDecl *fn, const SourceManager &sm, bool system) const {
        std::string name = fn->getName().str();
        if (!in_smith_file(sm)) return name;
        if (system) return "c::" + name;
        OptionalFileEntryRef main = sm.getFileEntryRefForID(sm.getMainFileID());
        std::shared_ptr<const ::smith::FileNames> names = ::smith::namesOf(main->getName().str());
        const ::smith::WrittenName *found = names != nullptr ? names->find(name) : nullptr;
        return found != nullptr ? (found->written.empty() ? found->member : found->written) : "c::" + name;
    }

    // The place of a reference: the macro that made it when the code is a macro's, else the token.
    static SourceLocation place(const SourceManager &sm, SourceLocation at) {
        return sm.getExpansionLoc(at);
    }

    // In a .smith file a C function is written `c::name`: the finding is at the `c`, where the call begins.
    static SourceLocation written_place(const SourceManager &sm, SourceLocation at, bool system) {
        SourceLocation where = place(sm, at);
        if (!system || !in_smith_file(sm) || !where.isFileID()) return where;
        SourceLocation before = where.getLocWithOffset(-3);
        bool invalid = false;
        llvm::StringRef text = sm.getCharacterData(before, &invalid);
        if (!invalid && text.starts_with("c::")) return before;
        return where;
    }

    class Walker final : public RecursiveASTVisitor<Walker> {
      public:
        Walker(Effects &owner, const FunctionDecl *fn, ASTContext &context, bool implementation, bool failure_path,
               Info *info)
            : owner(owner), fn(fn), sm(context.getSourceManager()), context(context), implementation(implementation),
              failure_path(failure_path), info(info) {}

        bool VisitDeclRefExpr(DeclRefExpr *ref) {
            const auto *callee = dyn_cast<FunctionDecl>(ref->getDecl());
            if (callee == nullptr) return true;
            owner.reference(*this, callee, ref->getExprLoc());
            return true;
        }

        bool VisitObjCMessageExpr(ObjCMessageExpr *message) {
            if (failure_path) return true;
            const ObjCInterfaceDecl *interface = message->getReceiverInterface();
            const ObjCMethodDecl *method = message->getMethodDecl();
            bool system = false;
            if (interface != nullptr) {
                system = sm.isInSystemHeader(interface->getLocation());
            } else if (method != nullptr) {
                system = sm.isInSystemHeader(method->getLocation());
            }
            if (!system) return true;
            std::string receiver = interface != nullptr ? interface->getName().str() : "id";
            std::string text = "[" + receiver + " " + message->getSelector().getAsString() + "]";
            owner.effect(*this, message->getBeginLoc(), text, nullptr, true);
            return true;
        }

        Effects &owner;
        const FunctionDecl *fn;
        const SourceManager &sm;
        ASTContext &context;
        bool implementation;
        bool failure_path;
        Info *info;
    };

    void effect(Walker &walker, SourceLocation at, const std::string &callee, const Replacement *replacement,
                bool objc) {
        if (walker.implementation) {
            walker.info->effects.push_back({place(walker.sm, at), callee, replacement, objc});
            return;
        }
        SourceLocation where = written_place(walker.sm, at, !objc);
        if (!reported.insert(where.getRawEncoding()).second) return;
        if (objc) {
            diag(where,
                 "`%0` touches the world: wrap it in a capability's implementation (a module that defines a type "
                 "marked [[clang::annotate(\"smith::capability\")]]) and take that capability as a parameter")
                << callee;
        } else if (replacement != nullptr && replacement->capability == nullptr) {
            diag(where, "`%0` touches the world: %1") << callee << replacement->how;
        } else if (replacement != nullptr) {
            diag(where, "`%0` touches the world: take a `%1 *` from main's SmithIo and %2")
                << callee << replacement->capability << replacement->how;
        } else {
            diag(where, "`%0` touches the world: take the capability that does this (a SmithFiles *, SmithConsole *, "
                        "SmithClock *, SmithProcesses *, SmithNet *, SmithArena * or SmithKey) from main's SmithIo, or "
                        "make the call only inside a capability's implementation")
                << callee;
        }
    }

    void reference(Walker &walker, const FunctionDecl *callee, SourceLocation at) {
        const SourceManager &sm = walker.sm;
        if (function_has_annotation(callee, "smith::main_only")) {
            bool in_main = walker.fn->isMain();
            if (!in_main) {
                SourceLocation where = written_place(sm, at, true);
                if (reported.insert(where.getRawEncoding()).second)
                    diag(where, "`%0` makes a real capability and may be called only in main: take the capability as "
                                "a parameter (a test makes a fake)")
                        << written(callee, sm, false);
            }
            return;
        }
        if (walker.failure_path) return;
        Verdict verdict = classify(callee, sm);
        if (verdict == Verdict::Other) {
            if (walker.info != nullptr) walker.info->callees.push_back(callee->getCanonicalDecl());
            return;
        }
        if (verdict != Verdict::Raw) return;
        std::string name = written(callee, sm, true);
        // A call the user wrote as the argument of a macro (assert(open(...) == 0)) is at the token they wrote; one a
        // macro's body makes (getc) is at the macro, named as the user wrote it.
        if (at.isMacroID() && sm.isMacroArgExpansion(at)) at = sm.getSpellingLoc(at);
        if (at.isMacroID()) {
            llvm::StringRef macro = Lexer::getImmediateMacroNameForDiagnostics(at, sm, walker.context.getLangOpts());
            if (!macro.empty() && macro != callee->getName()) name = (in_smith_file(sm) ? "c::" : "") + macro.str();
        }
        effect(walker, at, name, replacement_for(callee->getName()), false);
    }

    void function(const FunctionDecl *fn, const ast_matchers::MatchFinder::MatchResult &Result) {
        ASTContext &context = *Result.Context;
        const SourceManager &sm = *Result.SourceManager;
        scan(context, sm);
        bool private_instance = false;
        bool implementation = implements(home_of(fn, sm, private_instance));
        bool failure_path =
            is_failure_name(fn->getName()) || fn->isNoReturn(); // a function that stops the program may say why
        Info scratch{fn, {}, {}};
        Walker walker(*this, fn, context, implementation, failure_path, &scratch);
        walker.TraverseStmt(fn->getBody());
        if (!implementation || failure_path) return;
        if (!fn->isExternallyVisible() || private_instance) {
            capability_functions[fn->getCanonicalDecl()] = std::move(scratch);
            return;
        }
        bool capable = holds(fn->getReturnType());
        for (const ParmVarDecl *parameter : fn->parameters())
            capable = capable || holds(parameter->getType());
        scratch.effects.shrink_to_fit();
        exported.push_back({fn, capable});
        capability_functions[fn->getCanonicalDecl()] = std::move(scratch);
    }

    std::vector<std::pair<const FunctionDecl *, bool>> exported;

    // Rule 4, at the end of the unit, when every static function of the file has been seen.
    void finish() {
        for (auto &[fn, capable] : exported) {
            if (capable) continue;
            const Effect *first = find_effect(fn);
            if (first == nullptr) continue;
            const Replacement *how = first->replacement;
            std::string advice =
                how != nullptr && how->capability != nullptr
                    ? std::string("take a capability, such as the `") + how->capability + " *` it needs"
                    : std::string("take the capability it needs");
            diag(fn->getLocation(), "`%0` is exported by a capability's implementation and reaches `%1`, but takes and "
                                    "returns no capability, so any caller could touch the world: %2, or make it static")
                << fn->getName() << first->callee << advice;
        }
        exported.clear();
    }

    const Effect *find_effect(const FunctionDecl *start) {
        std::set<const FunctionDecl *> seen;
        std::vector<const FunctionDecl *> pending{start->getCanonicalDecl()};
        while (!pending.empty()) {
            const FunctionDecl *current = pending.back();
            pending.pop_back();
            if (!seen.insert(current).second) continue;
            auto found = capability_functions.find(current);
            if (found == capability_functions.end()) continue;
            if (!found->second.effects.empty()) return &found->second.effects.front();
            for (const FunctionDecl *callee : found->second.callees) {
                auto target = capability_functions.find(callee);
                if (target == capability_functions.end()) continue;
                if (callee == start->getCanonicalDecl() || !callee->isExternallyVisible()) pending.push_back(callee);
            }
        }
        return nullptr;
    }

    void storage(const VarDecl *var) {
        if (!holds(var->getType())) return;
        diag(var->getLocation(),
             "a capability cannot be kept in static storage (`%0`); pass it as a parameter from main")
            << var->getName();
    }
};

} // namespace clang::tidy::smith_effects
