// `smith prove`'s first solver. Z3 is what Alive2 builds its queries with, but on the queries of a function with many
// stores and loads through one array it takes minutes where Bitwuzla takes a fraction of a second.
// Alive2 prints a query as SMT-LIB 2 and this asks Bitwuzla, in this process, whether it is satisfiable.
#include "prove.hpp"

#include "bitwuzla/cpp/bitwuzla.h"
#include "bitwuzla/cpp/parser.h"
#include "llvm/Support/SHA256.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <sstream>

// The step store (runtime/executor_cache.c): the answers of the solver are entries in it, beside the compiles (#3174).
extern "C" bool smith_stepcache_answer_get(const char *hex, char *out, size_t capacity, size_t *size);
extern "C" void smith_stepcache_answer_put(const char *hex, const char *bytes, size_t size);

namespace prove {
namespace {

// Stops the solver at a budget of CPU time (see bitwuzla_check).
class Deadline final : public bitwuzla::Terminator {
  public:
    Deadline(double seconds, double process_start, unsigned threads)
        : seconds(seconds), threads(threads), process_start(process_start), thread_start(thread_cpu_seconds()) {}
    bool terminate() override {
        expired = expired || thread_cpu_seconds() - thread_start >= seconds ||
                  (process_cpu_seconds() - process_start) / threads >= seconds;
        return expired;
    }
    bool expired = false;

  private:
    double seconds;
    unsigned threads;
    double process_start;
    double thread_start;
};

} // namespace

std::string answer_key(const char *kind, const std::string &text) {
    llvm::SHA256 hash;
    hash.update(std::string("smith-answer ") + kind + "\n");
    hash.update(text);
    std::array<uint8_t, 32> digest = hash.final();
    static const char *const hex = "0123456789abcdef";
    std::string name;
    for (uint8_t byte : digest) {
        name += hex[byte >> 4];
        name += hex[byte & 15];
    }
    return name;
}

bool answer_get(const std::string &key, std::string &bytes) {
    static constexpr size_t capacity = 1 << 16;
    char buffer[capacity];
    size_t size = 0;
    if (!smith_stepcache_answer_get(key.c_str(), buffer, capacity, &size)) return false;
    bytes.assign(buffer, size);
    return true;
}

void answer_put(const std::string &key, const std::string &bytes) {
    smith_stepcache_answer_put(key.c_str(), bytes.data(), bytes.size());
}

// "unsat <cpu>" or "sat <cpu>": the answer and the CPU seconds the query took.
Verdict bitwuzla_check(const std::string &query, double seconds, double process_start, unsigned threads,
                       bool read_store, double &cpu) {
    cpu = -1;
    // Bitwuzla's version is what decides the answer besides the query (Sat and Unsat do not depend on the options that
    // only change how long it takes); a Timeout or an Unknown is no answer and is not kept.
    const std::string key = answer_key((std::string("bitwuzla ") + bitwuzla::version()).c_str(), query);
    std::string kept;
    if (read_store && answer_get(key, kept)) {
        char word[8] = {};
        double took = 0;
        if (std::sscanf(kept.c_str(), "%7s %lf", word, &took) == 2 && (word[0] == 's' || word[0] == 'u')) {
            if (took > seconds) return Verdict::Timeout;
            cpu = took;
            return word[0] == 'u' ? Verdict::Unsat : Verdict::Sat;
        }
    }
    const double before = thread_cpu_seconds();
    Verdict verdict = Verdict::Unknown;
    try {
        bitwuzla::TermManager manager;
        bitwuzla::Options options;
        std::ostringstream answer;
        bitwuzla::parser::Parser parser(manager, options, "smt2", &answer);
        Deadline deadline(seconds, process_start, threads);
        parser.configure_terminator(&deadline);
        parser.parse(query, false, false);
        std::string text = answer.str();
        if (deadline.expired)
            verdict = Verdict::Timeout;
        else if (text.find("unsat") != std::string::npos)
            verdict = Verdict::Unsat;
        else if (text.find("sat") != std::string::npos)
            verdict = Verdict::Sat;
    } catch (const std::exception &failure) {
        if (std::getenv("SMITH_PROVE_DUMP") != nullptr) std::fprintf(stderr, "bitwuzla: %s\n", failure.what());
    } catch (...) {
    }
    if (verdict == Verdict::Unsat || verdict == Verdict::Sat) {
        char line[64];
        std::snprintf(line, sizeof line, "%s %.6f\n", verdict == Verdict::Unsat ? "unsat" : "sat",
                      thread_cpu_seconds() - before);
        answer_put(key, line);
    }
    return verdict;
}

} // namespace prove
