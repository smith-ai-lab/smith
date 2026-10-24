// How the debugger shows a run-time array `T xs[:]` (the struct smith_array_<element>_n { T *data; usize count[1]; }):
// as its elements, with its count, not as a pointer and a count array. The formatters are lldb's own C++ ones, the
// kind it ships for std::span, registered in its default category when `smith dbg` or `smith dap` starts, so no Python
// is involved and nothing in LLVM changes: the editor's variables view, `frame variable` and `smith mcp` all use them.
//   (i32[:]) xs = count=3 { [0] = 1 [1] = 2 [2] = 3 }
//   (char[:]) name = "abc" count=3 { [0] = 'a' ... }
// The type's name is the written one (`i32[:]`, `f32[:][2]`, `const char[:]`, `u8[restrict :]`), taken from the
// struct's name; a row of a many-dimensional one is an array of the other dimensions. lldb's static initializers are
// deferred (tool.h): this runs after they have.
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
constexpr size_t dims_max = 8;

// What a slice's struct name says: `smith_array_[restrict_][const_]<element>_n[_<n or size>...]`, one `n` or fixed size
// for each dimension, the first always a run-time one. `f32[:][2]` is `smith_array_f32_n_2`, `const u8[restrict :]` is
// `smith_array_restrict_const_u8_n`.
struct Tag {
    bool valid = false;
    bool restrict_data = false;
    bool const_slice = false; // `[const :]`: the struct itself is const, which the name does not say
    std::string element;      // as written: `u8`, `const char`
    size_t count = 0;         // the number of dimensions
    bool runtime[dims_max] = {};
    uint64_t fixed[dims_max] = {};
};

// The dimension groups at the end of a name, `_n_2` of `f32_n_2`, taken off it; false when it has no run-time first.
bool take_dimensions(llvm::StringRef &rest, Tag &tag) {
    bool runtime[dims_max];
    uint64_t fixed[dims_max];
    size_t found = 0;
    size_t first_n = 0;
    for (;;) {
        size_t cut = rest.rfind('_');
        if (cut == llvm::StringRef::npos || found == dims_max) break;
        llvm::StringRef group = rest.substr(cut + 1);
        uint64_t size = 0;
        bool is_n = group == "n";
        if (!is_n && (group.empty() || group.getAsInteger(10, size))) break;
        runtime[found] = is_n;
        fixed[found] = size;
        found += 1;
        if (is_n) first_n = found;
        rest = rest.substr(0, cut);
    }
    if (first_n == 0) return false;
    // groups are collected last first; a size left of the first `n` belongs to the element (`vec_2_n`)
    for (size_t at = first_n; at < found; at++)
        rest = llvm::StringRef(rest.data(), rest.size() + 1 + (runtime[at] ? 1 : std::to_string(fixed[at]).size()));
    tag.count = first_n;
    for (size_t at = 0; at < first_n; at++) {
        tag.runtime[at] = runtime[first_n - 1 - at];
        tag.fixed[at] = fixed[first_n - 1 - at];
    }
    return true;
}

Tag parse_tag(llvm::StringRef name, bool const_slice) {
    Tag tag;
    name.consume_front("const ");
    name.consume_front("struct ");
    if (!name.consume_front(prefix) || !take_dimensions(name, tag)) return tag;
    tag.restrict_data = name.consume_front("restrict_");
    std::string element;
    while (true) {
        if (name.consume_front("const_"))
            element += "const ";
        else if (name.consume_front("volatile_"))
            element += "volatile ";
        else
            break;
    }
    if (name.empty()) return tag;
    tag.element = element + name.str();
    tag.const_slice = const_slice;
    tag.valid = true;
    return tag;
}

// `i32[:]`, `const char[:]`, `f32[:][2]`, `u8[restrict :]`, `u8[const :]`: as the language writes them.
std::string written_name(const Tag &tag) {
    std::string text = tag.element;
    for (size_t at = 0; at < tag.count; at++) {
        text += '[';
        if (at == 0 && (tag.const_slice || tag.restrict_data))
            text += std::string(tag.const_slice ? "const" : "") + (tag.const_slice && tag.restrict_data ? " " : "") +
                    (tag.restrict_data ? "restrict" : "") + " ";
        text += tag.runtime[at] ? ":" : std::to_string(tag.fixed[at]);
        text += ']';
    }
    return text;
}

// A slice read from its value: the elements' type and address, and each dimension's size.
struct Slice {
    Tag tag;
    lldb::ValueObjectSP data;
    CompilerType element;
    uint64_t sizes[dims_max] = {};
    bool ok = false;

    explicit Slice(ValueObject &value) {
        tag = parse_tag(value.GetTypeName().GetStringRef(), value.GetCompilerType().IsConst());
        data = value.GetChildMemberWithName("data");
        lldb::ValueObjectSP counts = value.GetChildMemberWithName("count");
        if (!tag.valid || !data || !counts) return;
        size_t next = 0;
        for (size_t at = 0; at < tag.count; at++) {
            if (!tag.runtime[at]) {
                sizes[at] = tag.fixed[at];
                continue;
            }
            lldb::ValueObjectSP size = counts->GetChildAtIndex(next++);
            if (!size) return;
            sizes[at] = size->GetValueAsUnsigned(0);
        }
        element = data->GetCompilerType().GetPointeeType();
        ok = true;
    }

    // The type of one child of the first dimension: the element, or the row (an array of the other dimensions).
    CompilerType child_type() const {
        CompilerType type = element;
        for (size_t at = tag.count; at > 1; at--)
            type = type.GetArrayType(sizes[at - 1]);
        return type;
    }
};

class SliceFrontEnd : public SyntheticChildrenFrontEnd {
  public:
    explicit SliceFrontEnd(ValueObject &backend) : SyntheticChildrenFrontEnd(backend) {
        Update();
    }

    llvm::Expected<uint32_t> CalculateNumChildren() override {
        return count > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(count);
    }

    lldb::ValueObjectSP GetChildAtIndex(uint32_t index) override {
        if (!data || index >= count || child_size == 0) return {};
        uint64_t address = data->GetValueAsUnsigned(0) + static_cast<uint64_t>(index) * child_size;
        std::string name = "[" + std::to_string(index) + "]";
        return CreateChildValueObjectFromAddress(name, address, m_backend.GetExecutionContextRef(), child);
    }

    lldb::ChildCacheState Update() override {
        Slice slice(m_backend);
        tag = slice.tag;
        data = slice.ok ? slice.data : nullptr;
        count = slice.ok ? slice.sizes[0] : 0;
        child_size = 0;
        if (slice.ok) {
            child = slice.child_type();
            if (auto size = child.GetByteSize(nullptr)) child_size = *size;
        }
        return lldb::ChildCacheState::eRefetch;
    }

    bool MightHaveChildren() override {
        return true;
    }

    llvm::Expected<size_t> GetIndexOfChildWithName(ConstString name) override {
        auto index = formatters::ExtractIndexFromString(name.GetCString());
        if (!index) return llvm::createStringErrorV("type has no child named '{0}'", name);
        return *index;
    }

    ConstString GetSyntheticTypeName() override {
        return tag.valid ? ConstString(written_name(tag)) : ConstString();
    }

  private:
    Tag tag;
    lldb::ValueObjectSP data;
    CompilerType child;
    uint64_t child_size = 0;
    uint64_t count = 0;
};

SyntheticChildrenFrontEnd *make_front_end(CXXSyntheticChildren *, lldb::ValueObjectSP backend) {
    return backend ? new SliceFrontEnd(*backend) : nullptr;
}

void put_escaped(Stream &stream, unsigned char byte) {
    switch (byte) {
    case '"':
        stream.PutCString("\\\"");
        break;
    case '\\':
        stream.PutCString("\\\\");
        break;
    case '\n':
        stream.PutCString("\\n");
        break;
    case '\t':
        stream.PutCString("\\t");
        break;
    default:
        if (byte >= 32 && byte < 127)
            stream.PutChar(static_cast<char>(byte));
        else
            stream.Printf("\\x%02x", byte);
    }
}

// `count=3`, `count=3x2` for `f32[:][2]`, and for bytes also their text: "abc" (at most 256 of them, then ...).
bool summarize(ValueObject &shown, Stream &stream, const TypeSummaryOptions &) {
    lldb::ValueObjectSP real = shown.GetNonSyntheticValue(); // the struct, whose members are data and count
    ValueObject &value = real ? *real : shown;
    Slice slice(value);
    if (!slice.ok) return false;
    uint64_t count = slice.sizes[0];
    auto size = slice.element.GetByteSize(nullptr);
    lldb::ProcessSP process = value.GetProcessSP();
    uint64_t address = slice.data->GetValueAsUnsigned(0);
    if (slice.tag.count == 1 && size && *size == 1 && slice.element.IsCharType() && process && count > 0 &&
        address != 0) {
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
    for (size_t at = 1; at < slice.tag.count; at++)
        stream.Printf("x%" PRIu64, slice.sizes[at]);
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
                                "^(const )?(struct )?smith_array_.+_n(_[0-9n]+)*$", synthetic, true);
    TypeSummaryImpl::Flags summary;
    summary.SetCascades(true).SetDontShowChildren(false).SetSkipPointers(false).SetSkipReferences(false);
    formatters::AddCXXSummary(category, summarize, "smith run-time array T xs[:]: its count",
                              "^(const )?(struct )?smith_array_.+_n(_[0-9n]+)*$", summary, true);
}
