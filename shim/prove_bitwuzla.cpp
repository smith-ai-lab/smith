// `smith prove`'s first solver. Z3 is what Alive2 builds its queries with, but on the queries of a function with many
// stores and loads through one array it takes minutes where Bitwuzla takes a fraction of a second.
// Alive2 prints a query as SMT-LIB 2 and this asks Bitwuzla, in this process, whether it is satisfiable.
#include "prove.hpp"

#include "bitwuzla/cpp/bitwuzla.h"
#include "bitwuzla/cpp/parser.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>

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

Verdict bitwuzla_check(const std::string &query, double seconds, double process_start, unsigned threads) {
    try {
        bitwuzla::TermManager manager;
        bitwuzla::Options options;
        std::ostringstream answer;
        bitwuzla::parser::Parser parser(manager, options, "smt2", &answer);
        Deadline deadline(seconds, process_start, threads);
        parser.configure_terminator(&deadline);
        parser.parse(query, false, false);
        if (deadline.expired) return Verdict::Timeout;
        std::string text = answer.str();
        if (text.find("unsat") != std::string::npos) return Verdict::Unsat;
        if (text.find("sat") != std::string::npos) return Verdict::Sat;
        return Verdict::Unknown;
    } catch (const std::exception &failure) {
        if (std::getenv("SMITH_PROVE_DUMP") != nullptr) std::fprintf(stderr, "bitwuzla: %s\n", failure.what());
        return Verdict::Unknown;
    } catch (...) {
        return Verdict::Unknown;
    }
}

} // namespace prove
