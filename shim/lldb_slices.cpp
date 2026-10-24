// How the debugger shows a run-time array `T xs[:]` (the struct smith_array_<element>_n { T *data; usize count[1]; }):
// as its elements, with its count, not as a pointer and a count array. The formatters are lldb's own C++ ones, the
// kind it ships for std::span, registered in its default category when `smith dbg` or `smith dap` starts, so no Python
// is involved and nothing in LLVM changes: the editor's variables view, `frame variable` and `smith mcp` all use them.
//   (i32[:]) xs = count=3 { [0] = 1 [1] = 2 [2] = 3 }
//   (char[:]) name = "abc" count=3 { [0] = 'a' ... }
// The type's name is the written one (`i32[:]`), taken from the struct's name. lldb's static initializers are deferred
// (tool.h): this runs after they have.
#include "lldb/DataFormatters/DataVisualization.h"
#include "lldb/DataFormatters/FormattersHelpers.h"
#include "lldb/DataFormatters/TypeSummary.h"
#include "lldb/DataFormatters/TypeSynthetic.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"
#include "lldb/ValueObject/ValueObject.h"

#include <cstdint>
#include <string>

using namespace lldb_private;

namespace {

constexpr const char *prefix = "smith_array_";
constexpr const char *suffix = "_n";

// `i32[:]` from `smith_array_i32_n`, "" when the name is not one of smith's.
std::string written_name(llvm::StringRef name) {
    name.consume_front("struct ");
    if (!name.consume_front(prefix) || !name.consume_back(suffix)) return "";
    return name.str() + "[:]";
}

class SliceFrontEnd : public SyntheticChildrenFrontEnd {
  public:
    explicit SliceFrontEnd(ValueObject &backend) : SyntheticChildrenFrontEnd(backend) { Update(); }

    llvm::Expected<uint32_t> CalculateNumChildren() override {
        return count > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(count);
    }

    lldb::ValueObjectSP GetChildAtIndex(uint32_t index) override {
        if (!data || index >= count || element_size == 0) return {};
        uint64_t address = data->GetValueAsUnsigned(0) + static_cast<uint64_t>(index) * element_size;
        std::string name = "[" + std::to_string(index) + "]";
        return CreateChildValueObjectFromAddress(name, address, m_backend.GetExecutionContextRef(), element);
    }

    lldb::ChildCacheState Update() override {
        data = m_backend.GetChildMemberWithName("data");
        lldb::ValueObjectSP counts = m_backend.GetChildMemberWithName("count");
        lldb::ValueObjectSP first = counts ? counts->GetChildAtIndex(0) : nullptr;
        count = first ? first->GetValueAsUnsigned(0) : 0;
        element_size = 0;
        if (data) {
            element = data->GetCompilerType().GetPointeeType();
            if (auto size = element.GetByteSize(nullptr)) element_size = *size;
        }
        return lldb::ChildCacheState::eRefetch;
    }

    bool MightHaveChildren() override { return true; }

    llvm::Expected<size_t> GetIndexOfChildWithName(ConstString name) override {
        auto index = formatters::ExtractIndexFromString(name.GetCString());
        if (!index) return llvm::createStringErrorV("type has no child named '{0}'", name);
        return *index;
    }

    ConstString GetSyntheticTypeName() override {
        std::string name = written_name(m_backend.GetTypeName().GetStringRef());
        return name.empty() ? ConstString() : ConstString(name);
    }

  private:
    lldb::ValueObjectSP data;
    CompilerType element;
    uint64_t element_size = 0;
    uint64_t count = 0;
};

SyntheticChildrenFrontEnd *make_front_end(CXXSyntheticChildren *, lldb::ValueObjectSP backend) {
    return backend ? new SliceFrontEnd(*backend) : nullptr;
}

void put_escaped(Stream &stream, unsigned char byte) {
    switch (byte) {
    case '"': stream.PutCString("\\\""); break;
    case '\\': stream.PutCString("\\\\"); break;
    case '\n': stream.PutCString("\\n"); break;
    case '\t': stream.PutCString("\\t"); break;
    default:
        if (byte >= 32 && byte < 127)
            stream.PutChar(static_cast<char>(byte));
        else
            stream.Printf("\\x%02x", byte);
    }
}

// `count=3`, and for bytes also their text: "abc" (at most 256 of them, then ...).
bool summarize(ValueObject &shown, Stream &stream, const TypeSummaryOptions &) {
    lldb::ValueObjectSP real = shown.GetNonSyntheticValue(); // the struct, whose members are data and count
    ValueObject &value = real ? *real : shown;
    lldb::ValueObjectSP data = value.GetChildMemberWithName("data");
    lldb::ValueObjectSP counts = value.GetChildMemberWithName("count");
    lldb::ValueObjectSP first = counts ? counts->GetChildAtIndex(0) : nullptr;
    if (!data || !first) return false;
    uint64_t count = first->GetValueAsUnsigned(0);
    CompilerType element = data->GetCompilerType().GetPointeeType();
    auto size = element.GetByteSize(nullptr);
    lldb::ProcessSP process = value.GetProcessSP();
    uint64_t address = data->GetValueAsUnsigned(0);
    if (size && *size == 1 && element.IsCharType() && process && count > 0 && address != 0) {
        uint64_t shown = count < 256 ? count : 256;
        std::string bytes(shown, '\0');
        Status error;
        if (process->ReadMemory(address, bytes.data(), shown, error) == shown) {
            stream.PutChar('"');
            for (char byte : bytes)
                put_escaped(stream, static_cast<unsigned char>(byte));
            stream.PutChar('"');
            if (shown < count) stream.PutCString("...");
            stream.PutChar(' ');
        }
    }
    stream.Printf("count=%" PRIu64, count);
    return true;
}

} // namespace

// Registers the formatters of `T xs[:]`; once is enough, and calling it again changes nothing.
extern "C" void smith_lldb_slice_formatters(void) {
    static bool done = false;
    if (done) return;
    done = true;
    lldb::TypeCategoryImplSP category;
    if (!DataVisualization::Categories::GetCategory(ConstString("default"), category) || !category) return;
    SyntheticChildren::Flags synthetic;
    synthetic.SetCascades(true).SetSkipPointers(false).SetSkipReferences(false);
    formatters::AddCXXSynthetic(category, make_front_end, "smith run-time array T xs[:]: its elements",
                                "^(struct )?smith_array_.+_n$", synthetic, true);
    TypeSummaryImpl::Flags summary;
    summary.SetCascades(true).SetDontShowChildren(false).SetSkipPointers(false).SetSkipReferences(false);
    formatters::AddCXXSummary(category, summarize, "smith run-time array T xs[:]: its count",
                              "^(struct )?smith_array_.+_n$", summary, true);
}
