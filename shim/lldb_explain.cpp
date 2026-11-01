// A C interface to lldb's SB API, for `smith --smith-explain` (runtime/explain.c): attach to a stopped test process,
// list the frames of the thread that holds the failure, read variables of one frame by path as `frame variable` does,
// and detach. Reading only: a path is a variable, its members and its indexes (`moves.count`, `board->cells[1][2]`); no
// expression is evaluated and no function is called. lldb's static initializers are deferred (tool.h).
#include "tool.h"

#include "lldb/API/LLDB.h"

#include <cstdio>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;
#include <cstring>
#include <string>

extern "C" {
#include "lldb_explain.h"
}

extern "C" void smith_lldb_slice_formatters(void);

namespace {

lldb::SBDebugger debugger;
lldb::SBProcess process;
lldb::SBThread thread;

void put(char *out, size_t capacity, const std::string &text) {
    std::snprintf(out, capacity, "%s", text.c_str());
}

bool names_stop(const char *name) {
    return name != nullptr && std::strstr(name, "std::test::stop") != nullptr;
}

// The thread that holds the failure: the one with smith's stop in its stack, else the first.
lldb::SBThread find_thread() {
    uint32_t count = process.GetNumThreads();
    for (uint32_t i = 0; i < count; i++) {
        lldb::SBThread candidate = process.GetThreadAtIndex(i);
        uint32_t frames = candidate.GetNumFrames();
        for (uint32_t f = 0; f < frames && f < 64; f++)
            if (names_stop(candidate.GetFrameAtIndex(f).GetFunctionName())) return candidate;
    }
    return process.GetThreadAtIndex(0);
}

std::string format(lldb::SBValue value, int depth);

std::string format_children(lldb::SBValue value, int depth, bool array) {
    std::string text = array ? "{" : "(";
    uint32_t count = value.GetNumChildren();
    for (uint32_t i = 0; i < count && i < 8; i++) {
        lldb::SBValue child = value.GetChildAtIndex(i);
        if (i > 0) text += ", ";
        if (!array && child.GetName() != nullptr) text += std::string(child.GetName()) + " = ";
        text += format(child, depth - 1);
    }
    if (count > 8) text += ", ...";
    return text + (array ? "}" : ")");
}

std::string format_pointer(lldb::SBValue value, int depth) {
    uint64_t address = value.GetValueAsUnsigned(0);
    if (address == 0) return "nullptr";
    char hex[32];
    std::snprintf(hex, sizeof hex, "0x%llx", static_cast<unsigned long long>(address));
    std::string text = hex;
    if (const char *summary = value.GetSummary()) return text + " " + summary;
    lldb::SBType pointee = value.GetType().GetPointeeType();
    if (depth <= 0 || !pointee.IsValid() || pointee.GetTypeClass() == lldb::eTypeClassFunction ||
        pointee.GetBasicType() == lldb::eBasicTypeVoid)
        return text;
    lldb::SBValue target = value.Dereference();
    if (!target.IsValid() || target.GetError().Fail()) return text + " (unreadable)";
    return text + " -> " + format(target, depth - 1);
}

// A `.smith` enumerator is lowered to C as `<module>_<name>`; the type is named as written (`board::Cell`), so the
// value is too: `board_DARK_P1` of `board::Cell` is `board::DARK_P1`.
std::string enumerator_as_written(const std::string &text, lldb::SBType type) {
    const char *name = type.GetName();
    std::string full = name != nullptr ? name : "";
    size_t last = full.rfind("::");
    if (last == std::string::npos) return text;
    std::string scope = full.substr(0, last + 2);
    std::string lowered = scope;
    for (size_t at = lowered.find("::"); at != std::string::npos; at = lowered.find("::", at + 1))
        lowered.replace(at, 2, "_");
    if (text.compare(0, lowered.size(), lowered) != 0) return text;
    return scope + text.substr(lowered.size());
}

// A one-byte integer is a number first (lldb shows `'\x02'`), and its character when it prints.
std::string format_byte(lldb::SBValue value) {
    int64_t number = value.GetType().IsTypeComplete() && (value.GetType().GetTypeFlags() & lldb::eTypeIsSigned)
                         ? value.GetValueAsSigned(0)
                         : static_cast<int64_t>(value.GetValueAsUnsigned(0));
    char text[32];
    std::snprintf(text, sizeof text, "%lld", static_cast<long long>(number));
    if (number >= 32 && number < 127) std::snprintf(text + std::strlen(text), 8, " '%c'", static_cast<char>(number));
    return text;
}

std::string format(lldb::SBValue value, int depth) {
    if (!value.IsValid()) return "<invalid>";
    if (value.GetError().Fail()) return "<unreadable>";
    lldb::SBType type = value.GetType();
    if (type.IsPointerType()) return format_pointer(value, depth);
    lldb::BasicType basic = type.GetBasicType();
    if (type.GetByteSize() == 1 &&
        (basic == lldb::eBasicTypeChar || basic == lldb::eBasicTypeSignedChar || basic == lldb::eBasicTypeUnsignedChar))
        return format_byte(value);
    if (const char *text = value.GetValue()) {
        if (type.GetCanonicalType().GetTypeClass() == lldb::eTypeClassEnumeration)
            return enumerator_as_written(text, type);
        if (const char *summary = value.GetSummary()) return std::string(text) + " " + summary;
        return text;
    }
    if (const char *summary = value.GetSummary()) return summary;
    if (value.GetNumChildren() > 0) {
        if (depth <= 0) return "...";
        return format_children(value, depth, type.IsArrayType());
    }
    return "<no value>";
}

} // namespace

namespace {

// Starts lldb in this process, with its own output silenced: the answer is the lines this process prints.
bool start_debugger(const char *self, char *problem, size_t capacity) {
    smith_tool_start("__smith_lldb", self);
    smith_lldb_slice_formatters();
    lldb::SBDebugger::Initialize();
    debugger = lldb::SBDebugger::Create(false);
    if (!debugger.IsValid()) {
        std::snprintf(problem, capacity, "lldb did not start");
        return false;
    }
    debugger.SetAsync(false);
    // lldb's packet timeout is 5 s of wall time: a loaded machine answers later and the attach fails. A generous one.
    debugger.HandleCommand("settings set plugin.process.gdb-remote.packet-timeout 600");
    if (FILE *quiet = std::fopen("/dev/null", "w")) {
        debugger.SetOutputFileHandle(quiet, true);
        debugger.SetErrorFileHandle(quiet, false);
    }
    return true;
}

} // namespace

extern "C" bool smith_explain_attach(long pid, const char *self, char *problem, size_t capacity) {
    if (!start_debugger(self, problem, capacity)) return false;
    lldb::SBTarget target = debugger.CreateTarget("");
    lldb::SBAttachInfo info(static_cast<lldb::pid_t>(pid));
    lldb::SBError error;
    process = target.Attach(info, error);
    if (!error.Success() || !process.IsValid()) {
        std::snprintf(problem, capacity, "%s", error.GetCString() != nullptr ? error.GetCString() : "attach failed");
        if (process.IsValid()) process.Detach(); // a test left attached stays stopped for ever
        return false;
    }
    thread = find_thread();
    if (!thread.IsValid()) {
        std::snprintf(problem, capacity, "no thread of the test is stopped at its check");
        process.Detach();
        return false;
    }
    return true;
}

extern "C" size_t smith_explain_frames(SmithExplainFrame *out, size_t capacity) {
    uint32_t count = thread.GetNumFrames();
    size_t used = 0;
    for (uint32_t i = 0; i < count && used < capacity; i++, used++) {
        lldb::SBFrame frame = thread.GetFrameAtIndex(i);
        lldb::SBLineEntry entry = frame.GetLineEntry();
        const char *name = frame.GetDisplayFunctionName();
        if (name == nullptr) name = frame.GetFunctionName();
        std::snprintf(out[used].function, sizeof out[used].function, "%s", name != nullptr ? name : "");
        out[used].file[0] = '\0';
        out[used].line = 0;
        if (entry.IsValid() && entry.GetFileSpec().IsValid()) {
            lldb::SBFileSpec spec = entry.GetFileSpec();
            std::snprintf(out[used].file, sizeof out[used].file, "%s/%s", spec.GetDirectory(), spec.GetFilename());
            out[used].line = entry.GetLine();
        }
    }
    return used;
}

extern "C" SmithExplainState smith_explain_read(size_t frame_index, const char *path, char *out, size_t capacity,
                                                long long *integer) {
    lldb::SBFrame frame = thread.GetFrameAtIndex(static_cast<uint32_t>(frame_index));
    lldb::SBValue value = frame.GetValueForVariablePath(path);
    if (!value.IsValid()) return SmithExplain_Missing;
    lldb::SBError error = value.GetError();
    if (error.Fail()) {
        const char *message = error.GetCString() != nullptr ? error.GetCString() : "";
        if (std::strstr(message, "optimized") != nullptr || std::strstr(message, "not available") != nullptr) {
            put(out, capacity, "<optimized out>");
            return SmithExplain_Optimized;
        }
        if (std::strstr(message, "memory") != nullptr || std::strstr(message, "read") != nullptr) {
            put(out, capacity, "<unreadable>");
            return SmithExplain_Unreadable;
        }
        return SmithExplain_Missing;
    }
    if (const char *text = value.GetValue()) {
        if (std::strstr(text, "not available") != nullptr || std::strstr(text, "optimized") != nullptr) {
            put(out, capacity, "<optimized out>");
            return SmithExplain_Optimized;
        }
    }
    if (integer != nullptr) *integer = value.GetValueAsSigned(0);
    put(out, capacity, format(value, 2));
    return SmithExplain_Ok;
}

extern "C" void smith_explain_detach(void) {
    if (process.IsValid()) process.Detach();
}

extern "C" size_t smith_explain_variables(size_t frame_index, SmithExplainVariable *out, size_t capacity) {
    lldb::SBFrame frame = thread.GetFrameAtIndex(static_cast<uint32_t>(frame_index));
    lldb::SBValueList list = frame.GetVariables(true, true, false, true);
    size_t used = 0;
    for (uint32_t i = 0; i < list.GetSize() && used < capacity; i++) {
        lldb::SBValue value = list.GetValueAtIndex(i);
        const char *name = value.GetName();
        if (name == nullptr || std::strncmp(name, "smith_", 6) == 0 || std::strncmp(name, "__", 2) == 0) continue;
        std::snprintf(out[used].label, sizeof out[used].label, "%s", name);
        put(out[used].value, sizeof out[used].value, format(value, 2));
        used += 1;
    }
    return used;
}

namespace {

lldb::SBTarget run_target;
lldb::SBBreakpoint run_breakpoint;
pid_t child = 0;

// The signal a Mach exception stands for, as a shell would see the program die of it.
int signal_of_exception(const char *description) {
    struct Row {
        const char *name;
        int signal;
    };
    static const Row rows[] = {{"EXC_BAD_ACCESS", 11},
                               {"EXC_BAD_INSTRUCTION", 4},
                               {"EXC_ARITHMETIC", 8},
                               {"EXC_BREAKPOINT", 5},
                               {"EXC_CRASH", 6}};
    for (const Row &row : rows)
        if (std::strstr(description, row.name) != nullptr) return row.signal;
    return 0;
}

// The thread that stopped the program: the first with a reason.
lldb::SBThread stopped_thread() {
    uint32_t count = process.GetNumThreads();
    for (uint32_t i = 0; i < count; i++) {
        lldb::SBThread candidate = process.GetThreadAtIndex(i);
        lldb::StopReason reason = candidate.GetStopReason();
        if (reason != lldb::eStopReasonNone && reason != lldb::eStopReasonInvalid) return candidate;
    }
    return process.GetThreadAtIndex(0);
}

// Resumes the program that lldb attached to, past the exec that starts it: on Linux the program stops itself, lldb
// attaches, and the exec that follows is a stop of its own (macOS starts the program suspended already executed).
void resume_first() {
    process.Continue();
    if (process.GetState() != lldb::eStateStopped) return;
    if (stopped_thread().GetStopReason() == lldb::eStopReasonExec) process.Continue();
}

void describe(SmithCrashStop *stop) {
    stop->state = SmithCrash_Exited;
    stop->status = 0;
    stop->at_breakpoint = false;
    stop->reason[0] = '\0';
    lldb::StateType state = process.GetState();
    if (state != lldb::eStateStopped && state != lldb::eStateCrashed) {
        stop->status = state == lldb::eStateExited ? process.GetExitStatus() : 1;
        return;
    }
    stop->state = SmithCrash_Stopped;
    thread = stopped_thread();
    lldb::StopReason reason = thread.GetStopReason();
    thread.GetStopDescription(stop->reason, sizeof stop->reason);
    stop->at_breakpoint = reason == lldb::eStopReasonBreakpoint;
    int number = 0;
    if (reason == lldb::eStopReasonSignal) number = static_cast<int>(thread.GetStopReasonDataAtIndex(0));
    if (reason == lldb::eStopReasonException) number = signal_of_exception(stop->reason);
    stop->status = number > 0 ? 128 + number : 1;
}

} // namespace

extern "C" bool smith_crash_start(const char *self, const char *const *args, const char *at_file, unsigned at_line,
                                  SmithCrashStop *stop, char *problem, size_t capacity) {
    if (!start_debugger(self, problem, capacity)) return false;
    lldb::SBError error;
    run_target = debugger.CreateTarget(args[0], nullptr, nullptr, true, error);
    if (!error.Success() || !run_target.IsValid()) {
        std::snprintf(problem, capacity, "%s", error.GetCString() != nullptr ? error.GetCString() : "no such program");
        return false;
    }
    if (at_file[0] != '\0') run_breakpoint = run_target.BreakpointCreateByLocation(at_file, at_line);
    // The program is started suspended by this process, with its standard streams as they are, and lldb attaches:
    // lldb's own launch gives the program a terminal of its own and merges its output.
    pid_t pid = 0;
    int spawned = smith_platform_spawn_suspended(&pid, args[0], const_cast<char *const *>(args), environ);
    if (spawned != 0) {
        std::snprintf(problem, capacity, "cannot run %s: %s", args[0], std::strerror(spawned));
        return false;
    }
    child = pid;
    lldb::SBAttachInfo info(static_cast<lldb::pid_t>(pid));
    process = run_target.Attach(info, error);
    if (error.Success()) kill(pid, SIGCONT);
    if (!error.Success() || !process.IsValid()) {
        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
        std::snprintf(problem, capacity, "%s", error.GetCString() != nullptr ? error.GetCString() : "launch failed");
        return false;
    }
    resume_first();
    describe(stop);
    return true;
}

extern "C" size_t smith_crash_locations(void) {
    return run_breakpoint.IsValid() ? run_breakpoint.GetNumLocations() : 0;
}

extern "C" void smith_crash_continue(SmithCrashStop *stop) {
    process.Continue();
    describe(stop);
}

extern "C" void smith_crash_kill(void) {
    if (process.IsValid()) process.Kill();
    if (child > 0) waitpid(child, nullptr, WNOHANG);
}
