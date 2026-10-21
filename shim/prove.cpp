// `smith prove <files> [-- <compile flags>]`: proves each function that carries a contract (<smith/contract.h>) on LLVM
// IR, with Alive2's encoding of IR semantics and Z3. smith.c calls smith_prove_main with the clang it runs in process.
//
// Each file is compiled to bitcode by that clang (in a child process of this one, so a crash or an exit is contained)
// with the checks of UBSan that name undefined behaviour in trap mode, debug info and no optimization; the files are
// linked into one module (shim/prove_ir.cpp), and each function with a contract is proved on its own
// (shim/prove_alive.cpp). The results print like a test run.
#include "prove.hpp"

#include "llvm/IR/Instructions.h"
#include "llvm/Support/raw_ostream.h"

#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/resource.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern "C" int smith_clang_main(const char *program, int argc, char **argv, bool lld, const char *resource);
extern "C" int smith_lang_command(int argc, char **argv, int first, char **out, size_t capacity);

namespace {

using Clock = std::chrono::steady_clock;

// The checks of UBSan that become explicit branches to a trap in the IR. null, alignment and object-size are left to
// Alive2's memory model, which treats an invalid access as undefined behaviour itself; pointer-overflow, bool and enum
// would make every pointer an integer or every loaded byte a contract. A trailing array is an array, not a flexible
// array member, so column 8 of an 8-wide row is out of bounds.
const char *const sanitizers = "-fsanitize=signed-integer-overflow,shift,integer-divide-by-zero,array-bounds,vla-bound,"
                               "unreachable,builtin,float-cast-overflow";

struct Arguments {
    std::vector<std::string> files;
    std::vector<std::string> flags;
    prove::Options options;
};

[[noreturn]] void usage(const char *message) {
    if (message != nullptr) std::fprintf(stderr, "smith: prove: %s\n", message);
    std::fprintf(stderr, "usage: smith prove [options] <file.c>... [-- <compile flags>]\n"
                         "  --function=NAME   prove only this function (repeatable)\n"
                         "  --budget=SECONDS  the time one function may take (default 10)\n"
                         "  -v                print the functions that are proved too, with their time\n");
    std::exit(2);
}

Arguments parse(int argc, char **argv) {
    Arguments arguments;
    bool flags = false;
    for (int i = 0; i < argc; i++) {
        std::string argument = argv[i];
        if (flags) {
            arguments.flags.push_back(argument);
        } else if (argument == "--") {
            flags = true;
        } else if (argument == "-v") {
            arguments.options.verbose = true;
        } else if (argument.rfind("--function=", 0) == 0) {
            arguments.options.only.push_back(argument.substr(11));
        } else if (argument.rfind("--budget=", 0) == 0) {
            arguments.options.budget_seconds = std::strtod(argument.c_str() + 9, nullptr);
            if (arguments.options.budget_seconds <= 0) usage("--budget wants a number of seconds");
        } else if (!argument.empty() && argument[0] == '-') {
            usage(("unknown option " + argument).c_str());
        } else {
            arguments.files.push_back(argument);
        }
    }
    if (arguments.files.empty()) usage("no files");
    arguments.options.dump = std::getenv("SMITH_PROVE_DUMP") != nullptr;
    return arguments;
}

bool is_smith(const std::string &file) {
    return file.size() > 6 && file.compare(file.size() - 6, 6, ".smith") == 0;
}

// Compiles one file to bitcode in `output` with the clang smith runs in process. Returns clang's exit status.
int compile(const char *program, const char *resource, const std::string &file, const std::vector<std::string> &flags,
            const std::string &output) {
    // A .smith file is C2y and lowered by the compiler's hook: smith_lang_command gives it `-x c` and the standard.
    bool smith = is_smith(file);
    std::vector<std::string> words = {"clang"};
    if (!smith) words.push_back("-std=gnu23");
    words.insert(words.end(), flags.begin(), flags.end());
    // After the caller's flags, so that they cannot turn the proof's choices off.
    for (const char *forced : {"-c", "-emit-llvm", "-O0", "-g", "-fno-discard-value-names", "-fstrict-flex-arrays=3",
                               "-UNDEBUG", "-Wno-unused-command-line-argument", sanitizers, "-fsanitize-trap=all"}) {
        words.emplace_back(forced);
    }
    words.insert(words.end(), {"-Xclang", "-disable-O0-optnone", "-o", output, file});
    std::vector<char *> pointers;
    for (std::string &word : words)
        pointers.push_back(word.data());
    pointers.push_back(nullptr);
    std::vector<char *> lowered(pointers.size() + 8);
    if (smith) {
        int count = smith_lang_command(static_cast<int>(pointers.size()) - 1, pointers.data(), 1, lowered.data(),
                                       lowered.size() - 1);
        if (count > 0) {
            pointers.assign(lowered.begin(), lowered.begin() + count);
            pointers.push_back(nullptr);
        }
    }
    std::fflush(nullptr);
    pid_t child = fork();
    if (child < 0) return 2;
    if (child == 0) {
        int status = smith_clang_main(program, static_cast<int>(pointers.size()) - 1, pointers.data(), false, resource);
        std::fflush(nullptr);
        _exit(status);
    }
    int status = 0;
    if (waitpid(child, &status, 0) < 0) return 2;
    if (WIFEXITED(status)) {
        if (WEXITSTATUS(status) == 0 && access(output.c_str(), R_OK) != 0) {
            std::fprintf(stderr, "smith: prove: compiling %s wrote no bitcode\n", file.c_str());
            return 2;
        }
        return WEXITSTATUS(status);
    }
    std::fprintf(stderr, "smith: prove: the compiler stopped on signal %d compiling %s\n", WTERMSIG(status),
                 file.c_str());
    return 2;
}

std::string at(const std::string &file, unsigned line) {
    return line == 0 ? file : file + ":" + std::to_string(line);
}

const char *describe(prove::Kind kind) {
    switch (kind) {
    case prove::Kind::Requires:
        return "smith_requires";
    case prove::Kind::Ensures:
        return "smith_ensures";
    case prove::Kind::Invariant:
        return "smith_invariant";
    case prove::Kind::Assert:
        return "assert";
    default:
        return "";
    }
}

void print_finding(const prove::Unit &unit, const prove::Finding &finding) {
    if (finding.point == nullptr) {
        std::printf(
            "  %s: undefined behaviour at `%s`: an access that may be invalid, or a value that may be poison\n",
            at(finding.file.empty() ? unit.file : finding.file, finding.line == 0 ? unit.line : finding.line).c_str(),
            finding.detail.c_str());
    } else {
        const prove::Point &point = *finding.point;
        switch (point.kind) {
        case prove::Kind::Requires: {
            const prove::CallSite &site = unit.call_sites[static_cast<size_t>(point.call_site)];
            std::printf("  %s: the call of %s may break %s(%s) (%s)\n", at(site.file, site.line).c_str(),
                        site.callee.c_str(), describe(point.kind), point.text.c_str(),
                        at(point.file, point.line).c_str());
            break;
        }
        case prove::Kind::Ensures:
        case prove::Kind::Invariant:
            std::printf("  %s: %s(%s) may be false\n", at(point.file, point.line).c_str(), describe(point.kind),
                        point.text.c_str());
            break;
        case prove::Kind::Assert:
            std::printf("  %s: assert(%s) may fail\n", at(point.file, point.line).c_str(), point.text.c_str());
            break;
        default:
            std::printf("  %s: undefined behaviour: %s\n", at(point.file, point.line).c_str(), point.text.c_str());
            break;
        }
        for (const std::string &outer : point.inlined)
            std::printf("      inlined from %s\n", outer.c_str());
    }
    if (!finding.values.empty()) {
        std::printf("      counterexample:");
        for (const auto &[name, value] : finding.values)
            std::printf(" %s = %s", name.c_str(), value.c_str());
        std::printf("\n");
    }
}

// Proves one function and prints what there is to say about it. Returns 0 when it is proved, else 1.
int prove_unit(prove::Program &loaded, const std::string &name, const prove::Options &options) {
    prove::Prepared prepared = prove::program_prepare(loaded, name);
    std::string label = prepared.unit.name + "  (" + at(prepared.unit.file, prepared.unit.line) + ")";
    if (prepared.unprovable != nullptr) {
        const prove::Unprovable &failure = *prepared.unprovable;
        std::printf("FAIL  %s\n  %s: cannot be proved: %s\n", label.c_str(), at(failure.file, failure.line).c_str(),
                    failure.what.c_str());
        if (!failure.fix.empty()) std::printf("      to prove it: %s\n", failure.fix.c_str());
        return 1;
    }
    if (options.dump) {
        prepared.function->print(llvm::errs());
        unsigned instructions = 0, loads = 0, stores = 0, blocks = 0;
        for (const llvm::BasicBlock &block : *prepared.function) {
            blocks++;
            for (const llvm::Instruction &instruction : block) {
                instructions++;
                loads += llvm::isa<llvm::LoadInst>(instruction);
                stores += llvm::isa<llvm::StoreInst>(instruction);
            }
        }
        std::fprintf(stderr, "ir of %s: %u blocks, %u instructions, %u loads, %u stores\n", prepared.unit.name.c_str(),
                     blocks, instructions, loads, stores);
    }
    prove::Outcome outcome = prove::alive_prove(prepared, options);
    int status = 1;
    switch (outcome.status) {
    case prove::Outcome::Proved:
        status = 0;
        if (options.verbose)
            std::printf("ok    %s  %.2f s wall, %.2f s CPU, charged %.2f s\n", label.c_str(), outcome.seconds,
                        outcome.cpu_seconds, outcome.charged);
        break;
    case prove::Outcome::Failed:
        std::printf("FAIL  %s  %.2f s wall, %.2f s CPU, charged %.2f s\n", label.c_str(), outcome.seconds,
                    outcome.cpu_seconds, outcome.charged);
        for (const prove::Finding &finding : outcome.findings)
            print_finding(prepared.unit, finding);
        break;
    case prove::Outcome::Timeout:
        std::printf(
            "FAIL  %s\n  over its budget of %.0f s of CPU time (counted as on %u idle cores): it was charged "
            "%.1f s, having used %.1f s of CPU in %.1f s of wall time, and was stopped; prove it in smaller "
            "pieces: a contract on the function that does most of the work lets callers use that instead of its "
            "body\n",
            label.c_str(), options.budget_seconds, prove::Options::solver_threads, outcome.charged, outcome.cpu_seconds,
            outcome.seconds);
        break;
    case prove::Outcome::Error:
        std::printf("FAIL  %s\n  %s\n", label.c_str(), outcome.error.c_str());
        break;
    }
    for (const std::string &note : prepared.unit.notes)
        std::printf("  %s\n", note.c_str());
    return status;
}

double cpu_of(const rusage &usage) {
    return static_cast<double>(usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) +
           static_cast<double>(usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1e6;
}

// How long the parent waits, in wall time, before it kills a proof's process: much more than the CPU budget needs,
// since a busy machine stretches wall time, and only for a process that is blocked rather than computing.
double wall_backstop(const prove::Options &options) {
    return 20 * options.budget_seconds + 30;
}

// What keeps a proof's process from outliving what started it or its budget. The solver stops itself at the CPU
// budget, but not always promptly, and a parent that is killed itself (a timeout around the whole command, a closed
// terminal) cannot kill the child, which busy in a solver would run on for ever with the parent's pid 1. So the child
// ends itself: at a limit of CPU time the kernel enforces (SIGXCPU), at a wall-clock limit for the time it spends
// blocked (SIGALRM), and as soon as its parent is gone (a thread that watches the parent pid).
void end_with_parent_or_budget(pid_t parent, const prove::Options &options) {
    rlim_t soft = static_cast<rlim_t>(options.cpu_limit());
    rlimit limit{soft, soft + 5};
    setrlimit(RLIMIT_CPU, &limit);
    alarm(static_cast<unsigned>(wall_backstop(options)) + 4);
    std::thread([parent] {
        for (;;) {
            if (getppid() != parent) _exit(3);
            usleep(20000);
        }
    }).detach();
}

// Every --function must name a function the prover read. One that names nothing is an error, never a skip: clang does
// not emit a static function that nothing calls, so a harness written as one is silently absent from the IR and the run
// would count as a success. One that is defined but is not a unit (no contract, not a caller of a contract) is an error
// too. Returns false after printing each such name.
bool check_wanted_functions(const prove::Program &loaded, const std::vector<std::string> &only) {
    const std::vector<std::string> units = prove::program_units(loaded);
    bool ok = true;
    for (const std::string &name : only) {
        bool defined = false;
        bool unit = false;
        for (const llvm::Function &function : *loaded.module) { // bounded by the functions of the module
            if (function.isDeclaration()) continue;
            std::string function_name = function.getName().str();
            if (prove::program_label(loaded, function_name) != name) continue;
            defined = true;
            for (const std::string &candidate : units)
                unit = unit || candidate == function_name;
        }
        if (unit) continue;
        ok = false;
        if (defined)
            std::fprintf(stderr,
                         "smith: prove: --function=%s: the function has no smith_requires, smith_ensures or "
                         "smith_invariant, and calls none that has one, so there is nothing to prove\n",
                         name.c_str());
        else
            std::fprintf(stderr,
                         "smith: prove: --function=%s: no function by that name in the code the prover read; if the "
                         "source defines it, it is a static function that nothing calls, which clang does not emit "
                         "(mark it [[gnu::used]] to keep it)\n",
                         name.c_str());
    }
    return ok;
}

// Proves each function in a process of its own, so that a crash or a function that will not finish is a failure of that
// function and nothing more, and the budget is a clock the prover cannot overrun. Returns 0 for a proof, 1 for a
// failure.
int prove_in_child(prove::Program &loaded, const std::string &name, const std::string &label,
                   const prove::Options &options) {
    std::fflush(nullptr);
    pid_t parent = getpid();
    pid_t child = fork();
    if (child < 0) return 1;
    if (child == 0) {
        end_with_parent_or_budget(parent, options);
        int status = prove_unit(loaded, name, options);
        std::fflush(nullptr);
        _exit(status);
    }
    Clock::time_point begin = Clock::now();
    double allowed = wall_backstop(options);
    int status = 0;
    rusage used{};
    for (;;) {
        pid_t done = wait4(child, &status, WNOHANG, &used);
        if (done == child) break;
        if (std::chrono::duration<double>(Clock::now() - begin).count() > allowed) {
            kill(child, SIGKILL);
            wait4(child, &status, 0, &used);
            std::printf(
                "FAIL  %s\n  blocked for %.0f s of wall time (the limit is %.0f s) after %.1f s of CPU: this is "
                "a bug in smith prove, not in the function\n",
                label.c_str(), allowed, allowed, cpu_of(used));
            return 1;
        }
        usleep(2000);
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status) == 0 ? 0 : 1;
    if (WIFSIGNALED(status) && WTERMSIG(status) == SIGXCPU) {
        std::printf(
            "FAIL  %s\n  over its budget of %.0f s of CPU time (counted as on %u idle cores): it has used "
            "%.1f s of CPU and was stopped; prove it in smaller pieces: a contract on the function that does most "
            "of the work lets callers use that instead of its body\n",
            label.c_str(), options.budget_seconds, prove::Options::solver_threads, cpu_of(used));
        return 1;
    }
    std::printf("FAIL  %s\n  the prover stopped on signal %d: this is a bug in smith prove, not in the function\n",
                label.c_str(), WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    return 1;
}

} // namespace

// Z3's static initializers, which scripts/prove-libs.sh moved out of the section dyld runs at load (there are ten
// thousand, and every smith process would pay for them) into __DATA,__smith_prove. `smith prove` is the one command
// that needs them.
static void run_deferred_initializers() {
    unsigned long size = 0;
    auto *table = reinterpret_cast<void (**)()>(getsectiondata(&_mh_execute_header, "__DATA", "__smith_prove", &size));
    for (size_t i = 0; table != nullptr && i < size / sizeof(void (*)()); i++)
        table[i]();
}

extern "C" int smith_prove_main(const char *program, const char *resource, int argc, char **argv) {
    run_deferred_initializers();
    Clock::time_point start = Clock::now();
    std::setvbuf(stdout, nullptr, _IOLBF, 0); // each result as soon as it is known
    Arguments arguments = parse(argc, argv);

    const char *temporary = std::getenv("TMPDIR");
    std::string pattern =
        std::string(temporary != nullptr && *temporary != '\0' ? temporary : "/tmp") + "/smith-prove-XXXXXX";
    std::vector<char> directory(pattern.begin(), pattern.end());
    directory.push_back('\0');
    if (mkdtemp(directory.data()) == nullptr) {
        std::fprintf(stderr, "smith: prove: cannot make a temporary folder\n");
        return 2;
    }
    // An `import foo` that finds no header looks for foo.smith among the command's .smith files first.
    std::string smith_inputs;
    for (const std::string &file : arguments.files) {
        if (is_smith(file)) smith_inputs += file + "\n";
    }
    if (!smith_inputs.empty()) setenv("SMITH_LANG_INPUTS", smith_inputs.c_str(), 1);
    std::vector<std::string> bitcode;
    int status = 0;
    for (size_t i = 0; i < arguments.files.size() && status == 0; i++) {
        std::string output = std::string(directory.data()) + "/" + std::to_string(i) + ".bc";
        status = compile(program, resource, arguments.files[i], arguments.flags, output);
        bitcode.push_back(output);
    }
    prove::Program loaded;
    std::string error;
    int result = 0;
    if (status != 0) {
        result = status;
    } else if (!prove::program_load(loaded, bitcode, error)) {
        std::fprintf(stderr, "smith: prove: %s\n", error.c_str());
        result = 2;
    }
    for (const std::string &path : bitcode)
        unlink(path.c_str());
    rmdir(directory.data());
    if (result != 0) return result;

    if (!check_wanted_functions(loaded, arguments.options.only)) return 1;
    unsigned proved = 0;
    unsigned failed = 0;
    for (const std::string &name : prove::program_units(loaded)) {
        std::string source_name = prove::program_label(loaded, name);
        if (!arguments.options.only.empty()) {
            bool wanted = false;
            for (const std::string &only : arguments.options.only)
                wanted = wanted || only == source_name;
            if (!wanted) continue;
        }
        (prove_in_child(loaded, name, source_name, arguments.options) == 0 ? proved : failed)++;
    }
    if (proved + failed == 0) {
        // A project that has written no contract yet has nothing to prove, which is not a failure (a --function that
        // names nothing was refused above).
        std::printf("smith: prove: nothing to prove: no function with smith_requires, smith_ensures or "
                    "smith_invariant\n");
        return 0;
    }
    double seconds = std::chrono::duration<double>(Clock::now() - start).count();
    std::printf("%u proved, %u failed in %.2f s\n", proved, failed, seconds);
    return failed == 0 ? 0 : 1;
}
