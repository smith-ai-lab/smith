// A C interface to lldb's SB API, for `smith --smith-explain` (runtime/explain.c): attach to a stopped test process,
// list the frames of the thread that holds the failure, read variables of one frame by path as `frame variable` does,
// and detach. Reading only: a path is a variable, its members and its indexes (`moves.count`, `board->cells[1][2]`); no
// expression is evaluated and no function is called. lldb's static initializers are deferred (tool.h).
#include "tool.h"

#include "lldb/API/LLDB.h"

#include <cstdio>
#include <cstring>
#include <string>

extern "C" {
#include "lldb_explain.h"
}

namespace {

lldb::SBDebugger debugger;
lldb::SBProcess process;
lldb::SBThread thread;

void put(char *out, size_t capacity, const std::string &text) {
    std::snprintf(out, capacity, "%s", text.c_str());
}

bool names_stop(const char *name) {
    return name != nullptr && std::strstr(name, "smith::test::stop") != nullptr;
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

std::string format(lldb::SBValue value, int depth) {
    if (!value.IsValid()) return "<invalid>";
    if (value.GetError().Fail()) return "<unreadable>";
    lldb::SBType type = value.GetType();
    if (type.IsPointerType()) return format_pointer(value, depth);
    if (const char *text = value.GetValue()) {
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

extern "C" bool smith_explain_attach(long pid, const char *self, char *problem, size_t capacity) {
    smith_tool_start("__smith_lldb", self);
    lldb::SBDebugger::Initialize();
    debugger = lldb::SBDebugger::Create(false);
    if (!debugger.IsValid()) {
        std::snprintf(problem, capacity, "lldb did not start");
        return false;
    }
    debugger.SetAsync(false);
    if (FILE *quiet = std::fopen("/dev/null", "w")) { // the answer is the lines this process prints, nothing lldb says
        debugger.SetOutputFileHandle(quiet, true);
        debugger.SetErrorFileHandle(quiet, false);
    }
    lldb::SBTarget target = debugger.CreateTarget("");
    lldb::SBAttachInfo info(static_cast<lldb::pid_t>(pid));
    lldb::SBError error;
    process = target.Attach(info, error);
    if (!error.Success() || !process.IsValid()) {
        std::snprintf(problem, capacity, "%s", error.GetCString() != nullptr ? error.GetCString() : "attach failed");
        return false;
    }
    thread = find_thread();
    return thread.IsValid();
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
