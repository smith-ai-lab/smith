// What `smith lsp` shows of a .smith file, written as the source writes it.
//
// clangd names things as the compiler sees them: `geometry_box_area` for the `box::area` the source says. The lowering
// (runtime/lang_names.c) keeps the way back, as a table of the names it made, and the hook (shim/hook.cpp) hands it
// over when a parse ends (shim/names.h). This is the layer between the editor and clangd that uses it: clangd reads its
// standard input and writes its standard output as it always did, and those are a pair of pipes to this layer, which
// reads the editor's messages and clangd's, and rewrites the names in the answers that show them: hover, the outline,
// completion (and what `x::` offers), signature help and the hints. It also leaves out of the answers that list places
// the generated headers (`.smith/include/*.h`), which are no place to go to or to rename in.
//
// The editor's own words are never rewritten: positions, ranges and the text of the documents are clangd's and the
// editor's, and are not names. A message that is not an answer to one of those requests passes as the bytes it came in.
#include "names.h"
#include "platform.h"

#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <poll.h>
#include <set>
#include <spawn.h>
#include <sstream>
#include <string>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <tuple>
#include <unistd.h>
#include <vector>

// shim/effects_infer.cpp: the plugin that judges the calls between files, told here what the editor does.
extern "C" void smith_effects_editor_start(void (*listener)(const char *source));
extern "C" void smith_effects_editor_object(const char *source, const char *object);
extern "C" void smith_effects_editor_saved(const char *source);
extern "C" void smith_effects_editor_pending(int pending);
extern char **environ;

namespace {

using llvm::StringRef;
using llvm::json::Array;
using llvm::json::Object;
using llvm::json::Value;

// How a name is shown: as the file writes it, as a member of the module it is behind (after `x::`), or with its whole
// module name (a place that is no file's).
enum class Style { Written, Member, Qualified };

std::string shown(const smith::WrittenName &name, Style style) {
    switch (style) {
    case Style::Member:
        return name.member;
    case Style::Qualified:
        return name.module.empty() ? name.written : name.module + "::" + name.member;
    case Style::Written:
        break;
    }
    return name.written;
}

bool identifierHead(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool identifierBody(char c) {
    return identifierHead(c) || (c >= '0' && c <= '9');
}

// The dimensions at the end of a slice's tag, `_n_2` of `f32_n_2`, as written (`[:][2]`) and taken off `rest`; "" when
// there is no run-time first one. A size left of the first `n` is the element's (`vec_2_n`).
std::string writtenDimensions(StringRef &rest, const char *qualifiers) {
    std::vector<std::string> groups; // last first
    StringRef left = rest;
    size_t first_n = 0;
    while (groups.size() < 8) {
        size_t cut = left.rfind('_');
        if (cut == StringRef::npos) break;
        StringRef group = left.substr(cut + 1);
        unsigned long long size = 0;
        bool runtime = group == "n";
        if (!runtime && (group.empty() || group.getAsInteger(10, size))) break;
        groups.push_back(runtime ? ":" : group.str());
        if (runtime) first_n = groups.size();
        left = left.substr(0, cut);
    }
    if (first_n == 0) return "";
    size_t kept = 0; // the length of the groups left of the first `n`, which go back to the element
    for (size_t at = first_n; at < groups.size(); at++)
        kept += 1 + groups[at].size();
    rest = StringRef(left.data(), left.size() + kept);
    std::string text;
    for (size_t at = first_n; at-- > 0;) {
        text += "[";
        if (at == first_n - 1 && *qualifiers != '\0') text += std::string(qualifiers) + " ";
        text += groups[at] + "]";
    }
    return text;
}

// `f32[:]` for the lowering's `smith_array_f32_n`, the struct of a run-time array (runtime/lang_slice.c); "" when the
// word is another. `smith_array_f32_n_2` is `f32[:][2]`, `smith_array_const_char_n` is `const char[:]`,
// `smith_array_restrict_u8_n` is `u8[restrict :]`, and `const_slice` (a `const struct ...`) is `[const :]`.
std::string writtenSlice(StringRef word, const smith::FileNames &names, Style style, bool const_slice = false) {
    if (!word.consume_front("smith_array_")) return "";
    bool restrict_data = word.consume_front("restrict_");
    std::string qualifiers = std::string(const_slice ? "const" : "") + (const_slice && restrict_data ? " " : "") +
                             (restrict_data ? "restrict" : "");
    std::string dimensions = writtenDimensions(word, qualifiers.c_str());
    if (dimensions.empty()) return "";
    std::string element_qualifiers;
    for (bool more = true; more;) {
        more = false;
        for (const char *keyword : {"const", "volatile"}) {
            std::string prefix = std::string(keyword) + "_";
            if (word.consume_front(prefix)) {
                element_qualifiers += std::string(keyword) + " ";
                more = true;
            }
        }
    }
    if (word.empty()) return "";
    const smith::WrittenName *element = word.contains('_') ? names.find(word) : nullptr;
    return element_qualifiers + (element != nullptr ? shown(*element, style) : word.str()) + dimensions;
}

// The text with the names of the table written in `style`. `moved` gets the old offset of each name changed and the new
// length less the old.
std::string rewritten(StringRef text, const smith::FileNames &names, Style style,
                      std::vector<std::pair<size_t, long>> *moved = nullptr) {
    std::string out;
    out.reserve(text.size());
    for (size_t at = 0; at < text.size();) {
        if (!identifierHead(text[at])) {
            out += text[at++];
            continue;
        }
        size_t end = at + 1;
        while (end < text.size() && identifierBody(text[end]))
            end += 1;
        StringRef word = text.substr(at, end - at);
        const StringRef keyword = "struct ";
        const StringRef constKeyword = "const struct ";
        bool constSlice = StringRef(out).ends_with(constKeyword);
        if (std::string slice = writtenSlice(word, names, style, constSlice); !slice.empty()) {
            size_t before = constSlice                                                          ? constKeyword.size()
                            : out.size() >= keyword.size() && StringRef(out).ends_with(keyword) ? keyword.size()
                                                                                                : 0;
            out.resize(out.size() - before); // `struct smith_array_f32_n` is just `f32[:]`, `const struct` `[const :]`
            if (moved != nullptr) moved->emplace_back(at, (long)slice.size() - (long)word.size() - (long)before);
            out += slice;
            at = end;
            continue;
        }
        const smith::WrittenName *name = word.contains('_') ? names.find(word) : nullptr;
        if (name != nullptr) {
            std::string replacement = shown(*name, style);
            if (moved != nullptr) moved->emplace_back(at, (long)replacement.size() - (long)word.size());
            out += replacement;
        } else {
            out += word;
        }
        at = end;
    }
    return out;
}

// A field of an object that is a string, rewritten.
void rewriteField(Object &object, StringRef key, const smith::FileNames &names, Style style) {
    Value *value = object.get(key);
    if (value == nullptr) return;
    if (auto text = value->getAsString()) *value = rewritten(*text, names, style);
}

// A label or a documentation: a string, or an object with a `value` (markup), or an array of those (inlay hint parts).
void rewriteText(Value &value, const smith::FileNames &names, Style style) {
    if (auto text = value.getAsString()) {
        value = rewritten(*text, names, style);
    } else if (Object *object = value.getAsObject()) {
        rewriteField(*object, "value", names, style);
    } else if (Array *array = value.getAsArray()) {
        for (Value &part : *array)
            rewriteText(part, names, style);
    }
}

// The text without the assembler labels of a module's names (` asm("_foo::f")`, which a generated header gives each
// export so that C reaches it): no one writes one, and the name above it is the name the source writes. A label of
// the system's headers (`asm("_fopen$DARWIN_EXTSN")`) has no `::` and stays.
std::string withoutLabels(StringRef text) {
    static const StringRef opening = " asm(\"";
    std::string out;
    out.reserve(text.size());
    while (!text.empty()) {
        size_t at = text.find(opening);
        if (at == StringRef::npos) break;
        size_t close = text.find("\")", at + opening.size());
        bool ours = close != StringRef::npos && text.slice(at + opening.size(), close).contains("::");
        out += text.take_front(at + (ours ? 0 : opening.size())).str();
        text = text.drop_front(ours ? close + 2 : at + opening.size());
    }
    out += text.str();
    return out;
}

// A hover's contents: markup (an object with a `value`), or a string, or an array of those.
void rewriteHover(Value &value, const smith::FileNames &names) {
    rewriteText(value, names, Style::Written);
    if (auto text = value.getAsString()) {
        value = withoutLabels(*text);
    } else if (Object *object = value.getAsObject()) {
        if (Value *inner = object->get("value")) {
            if (auto text = inner->getAsString()) *inner = withoutLabels(*text);
        }
    }
}

std::string pathOfUri(StringRef uri) {
    if (!uri.consume_front("file://")) return "";
    std::string path;
    for (size_t i = 0; i < uri.size(); i++) {
        if (uri[i] == '%' && i + 2 < uri.size() + 0 && std::isxdigit((unsigned char)uri[i + 1]) &&
            std::isxdigit((unsigned char)uri[i + 2])) {
            path += (char)std::stoi(uri.substr(i + 1, 2).str(), nullptr, 16);
            i += 2;
        } else {
            path += uri[i];
        }
    }
    return path;
}

// The generated headers of a project: places nobody means to go to.
bool isGenerated(StringRef uri) {
    return uri.contains("/.smith/include/");
}

// What the editor asked, kept until clangd answers.
struct Request {
    std::string method;
    std::string uri;
    int64_t line = -1;
    int64_t character = -1;
};

class Layer {
  public:
    // Reads one editor message: notes what it asks, so that the answer can be told from the others.
    void toServer(const std::string &body) {
        auto parsed = llvm::json::parse(body);
        if (!parsed) {
            llvm::consumeError(parsed.takeError());
            return;
        }
        Object *message = parsed->getAsObject();
        if (message == nullptr || message->get("id") == nullptr) return;
        auto method = message->getString("method");
        if (!method || !wanted(*method)) return;
        Request request;
        request.method = method->str();
        if (Object *params = message->getObject("params")) {
            if (Object *document = params->getObject("textDocument")) {
                if (auto uri = document->getString("uri")) request.uri = uri->str();
            }
            if (Object *position = params->getObject("position")) {
                request.line = position->getInteger("line").value_or(-1);
                request.character = position->getInteger("character").value_or(-1);
            }
        }
        std::lock_guard<std::mutex> guard(lock);
        pending[key(*message->get("id"))] = request;
    }

    // One clangd message; the bytes to send on.
    std::string toEditor(std::string body) {
        if (!StringRef(body).starts_with("{\"id\":")) return body; // a notification, or a request of clangd's
        auto id = llvm::json::parse(body.substr(6, body.find(',') - 6));
        if (!id) {
            llvm::consumeError(id.takeError());
            return body;
        }
        Request request;
        {
            std::lock_guard<std::mutex> guard(lock);
            auto found = pending.find(key(*id));
            if (found == pending.end()) return body;
            request = found->second;
            pending.erase(found);
        }
        auto parsed = llvm::json::parse(body);
        if (!parsed) {
            llvm::consumeError(parsed.takeError());
            return body;
        }
        Object *message = parsed->getAsObject();
        Value *result = message != nullptr ? message->get("result") : nullptr;
        if (message != nullptr && result != nullptr && calledGeneric(request, *message)) {
            return llvm::formatv("{0}", *parsed).str();
        }
        if (result == nullptr || result->kind() == Value::Null) return body;
        answer(request, *result);
        return llvm::formatv("{0}", *parsed).str();
    }

  private:
    std::mutex lock;
    std::map<std::string, Request> pending;

    // A hover or go-to-definition on the name in a call to a generic is the lowering's to answer: the compiler sees an
    // instance there, whose declaration is made of the call's own tokens. True when the answer was replaced.
    static bool calledGeneric(const Request &request, Object &message) {
        if (request.method != "textDocument/hover" && request.method != "textDocument/definition") return false;
        std::shared_ptr<const smith::FileNames> names = smith::namesOf(pathOfUri(request.uri));
        if (!names) return false;
        for (const smith::CallSite &site : names->calls) {
            if (request.line != site.line || request.character < site.column ||
                request.character > static_cast<int64_t>(site.column) + static_cast<int64_t>(site.length)) {
                continue;
            }
            Object start{{"line", static_cast<int64_t>(site.line)}, {"character", static_cast<int64_t>(site.column)}};
            Object end{{"line", static_cast<int64_t>(site.line)},
                       {"character", static_cast<int64_t>(site.column) + static_cast<int64_t>(site.length)}};
            if (request.method == "textDocument/hover") {
                message["result"] = Object{{"contents", Object{{"kind", "plaintext"}, {"value", site.hover}}},
                                           {"range", Object{{"start", std::move(start)}, {"end", std::move(end)}}}};
                return true;
            }
            Object from{{"line", static_cast<int64_t>(site.definition_line)},
                        {"character", static_cast<int64_t>(site.definition_column)}};
            Object to{{"line", static_cast<int64_t>(site.definition_line)},
                      {"character", static_cast<int64_t>(site.definition_column) + static_cast<int64_t>(site.length)}};
            message["result"] = Array{Object{{"uri", "file://" + site.file},
                                             {"range", Object{{"start", std::move(from)}, {"end", std::move(to)}}}}};
            return true;
        }
        return false;
    }

    static std::string key(const Value &id) {
        return llvm::formatv("{0}", id).str();
    }

    static bool wanted(StringRef method) {
        static const char *const methods[] = {
            "textDocument/hover",         "textDocument/completion", "textDocument/documentSymbol",
            "textDocument/signatureHelp", "textDocument/inlayHint",  "workspace/symbol",
            "textDocument/references",    "textDocument/rename",     "textDocument/definition"};
        for (const char *known : methods) {
            if (method == known) return true;
        }
        return false;
    }

    void answer(const Request &request, Value &result) {
        if (request.method == "workspace/symbol") return workspaceSymbols(result);
        if (request.method == "textDocument/references") return withoutGenerated(result);
        if (request.method == "textDocument/rename") return withoutGeneratedEdits(result);
        std::shared_ptr<const smith::FileNames> names = smith::namesOf(pathOfUri(request.uri));
        if (!names) return;
        if (request.method == "textDocument/hover") {
            if (Object *object = result.getAsObject()) {
                if (Value *contents = object->get("contents")) rewriteHover(*contents, *names);
            }
        } else if (request.method == "textDocument/documentSymbol") {
            symbols(result, *names, Style::Written);
        } else if (request.method == "textDocument/completion") {
            completion(request, result, *names);
        } else if (request.method == "textDocument/signatureHelp") {
            signatureHelp(result, *names);
        } else if (request.method == "textDocument/inlayHint") {
            if (Array *hints = result.getAsArray()) {
                for (Value &hint : *hints) {
                    if (Object *object = hint.getAsObject()) {
                        if (Value *label = object->get("label")) rewriteText(*label, *names, Style::Written);
                    }
                }
            }
        }
    }

    // The outline (a tree of symbols, or a flat list of them with their containers).
    static void symbols(Value &result, const smith::FileNames &names, Style style) {
        Array *list = result.getAsArray();
        if (list == nullptr) return;
        for (Value &entry : *list) {
            Object *symbol = entry.getAsObject();
            if (symbol == nullptr) continue;
            rewriteField(*symbol, "name", names, style);
            rewriteField(*symbol, "detail", names, style);
            rewriteField(*symbol, "containerName", names, style);
            if (Value *children = symbol->get("children")) symbols(*children, names, style);
        }
    }

    // Symbols of the whole project: each named as its file names it, with the module it is in.
    static void workspaceSymbols(Value &result) {
        Array *list = result.getAsArray();
        if (list == nullptr) return;
        Array kept;
        for (Value &entry : *list) {
            Object *symbol = entry.getAsObject();
            Object *location = symbol != nullptr ? symbol->getObject("location") : nullptr;
            StringRef uri = location != nullptr ? location->getString("uri").value_or("") : "";
            if (isGenerated(uri)) continue;
            if (std::shared_ptr<const smith::FileNames> names = smith::namesOf(pathOfUri(uri))) {
                rewriteField(*symbol, "name", *names, Style::Qualified);
            }
            kept.push_back(std::move(entry));
        }
        *list = std::move(kept);
    }

    static void withoutGenerated(Value &result) {
        Array *list = result.getAsArray();
        if (list == nullptr) return;
        Array kept;
        for (Value &entry : *list) {
            Object *location = entry.getAsObject();
            StringRef uri = location != nullptr ? location->getString("uri").value_or("") : "";
            if (!isGenerated(uri)) kept.push_back(std::move(entry));
        }
        *list = std::move(kept);
    }

    // A rename also edits the generated header that declares the name; the next build writes it again.
    static void withoutGeneratedEdits(Value &result) {
        Object *edit = result.getAsObject();
        if (edit == nullptr) return;
        if (Object *changes = edit->getObject("changes")) {
            std::vector<std::string> generated;
            for (auto &entry : *changes) {
                if (isGenerated(entry.first)) generated.push_back(entry.first.str());
            }
            for (const std::string &uri : generated)
                changes->erase(uri);
        }
        if (Array *documents = edit->getArray("documentChanges")) {
            Array kept;
            for (Value &entry : *documents) {
                Object *change = entry.getAsObject();
                Object *document = change != nullptr ? change->getObject("textDocument") : nullptr;
                StringRef uri = document != nullptr ? document->getString("uri").value_or("") : "";
                if (!isGenerated(uri)) kept.push_back(std::move(entry));
            }
            *documents = std::move(kept);
        }
    }

    // Signature help: the labels, and where each parameter is in them when it is given as an offset.
    static void signatureHelp(Value &result, const smith::FileNames &names) {
        Object *help = result.getAsObject();
        Array *signatures = help != nullptr ? help->getArray("signatures") : nullptr;
        if (signatures == nullptr) return;
        for (Value &entry : *signatures) {
            Object *signature = entry.getAsObject();
            if (signature == nullptr) continue;
            std::vector<std::pair<size_t, long>> moved;
            if (auto label = signature->getString("label"))
                *signature->get("label") = rewritten(*label, names, Style::Written, &moved);
            if (Value *documentation = signature->get("documentation"))
                rewriteText(*documentation, names, Style::Written);
            Array *parameters = signature->getArray("parameters");
            if (parameters == nullptr) continue;
            for (Value &parameter : *parameters) {
                Object *object = parameter.getAsObject();
                Value *label = object != nullptr ? object->get("label") : nullptr;
                if (label == nullptr) continue;
                if (Array *range = label->getAsArray()) {
                    for (Value &offset : *range) {
                        long old = offset.getAsInteger().value_or(0);
                        long shift = 0;
                        for (auto &[at, delta] : moved) {
                            if ((long)at < old) shift += delta;
                        }
                        offset = (int64_t)(old + shift);
                    }
                } else if (auto text = label->getAsString()) {
                    *label = rewritten(*text, names, Style::Written);
                }
            }
        }
    }

    // Whether a subsequence of `member` (any case) is what was typed.
    static bool matches(StringRef typed, StringRef member) {
        size_t at = 0;
        for (char c : member) {
            if (at < typed.size() && std::tolower((unsigned char)c) == std::tolower((unsigned char)typed[at])) at += 1;
        }
        return at == typed.size();
    }

    static StringRef firstName(StringRef text) {
        size_t start = 0;
        while (start < text.size() && !identifierHead(text[start]))
            start += 1;
        size_t end = start;
        while (end < text.size() && identifierBody(text[end]))
            end += 1;
        return text.substr(start, end - start);
    }

    static void completionItem(Object &item, const smith::FileNames &names, Style style) {
        for (StringRef key : {"label", "filterText", "insertText", "detail"})
            rewriteField(item, key, names, style);
        if (Value *documentation = item.get("documentation")) rewriteText(*documentation, names, style);
        if (Object *details = item.getObject("labelDetails")) {
            rewriteField(*details, "detail", names, style);
            rewriteField(*details, "description", names, style);
        }
        if (Object *edit = item.getObject("textEdit")) rewriteField(*edit, "newText", names, style);
        if (Array *more = item.getArray("additionalTextEdits")) {
            for (Value &entry : *more) {
                if (Object *object = entry.getAsObject()) rewriteField(*object, "newText", names, style);
            }
        }
    }

    // Completion. Anywhere, a name is offered as it is written. After `x::` (the lowering took the `x::` away and left
    // the cursor, so the parser offers every name) only the names of the module behind x are offered, each as its
    // member: the text typed is `x::` and a part of the member, and the edit replaces that part.
    void completion(const Request &request, Value &result, const smith::FileNames &names) {
        Array *items = result.getAsArray();
        if (items == nullptr) {
            if (Object *list = result.getAsObject()) items = list->getArray("items");
        }
        if (items == nullptr) return;
        smith::Qualifier qualifier = smith::qualifierOf(pathOfUri(request.uri));
        bool behind = qualifier.present && !qualifier.is_c && qualifier.line == (uint32_t)request.line &&
                      request.character >= (int64_t)qualifier.column - (int64_t)qualifier.partial.size() &&
                      request.character <= (int64_t)qualifier.column + (int64_t)qualifier.partial.size();
        if (qualifier.present && qualifier.is_c) return; // C's own names, as they are
        if (!behind) {
            for (Value &entry : *items) {
                if (Object *item = entry.getAsObject()) completionItem(*item, names, Style::Written);
            }
            return;
        }
        Array kept;
        for (Value &entry : *items) {
            Object *item = entry.getAsObject();
            if (item == nullptr) continue;
            StringRef label = item->getString("filterText").value_or(item->getString("label").value_or(""));
            const smith::WrittenName *name = names.find(firstName(label));
            if (name == nullptr || name->module != qualifier.module || !matches(qualifier.partial, name->member))
                continue;
            completionItem(*item, names, Style::Member);
            kept.push_back(std::move(entry));
        }
        *items = std::move(kept);
    }
};

bool sendFrame(int fd, const std::string &body);

// ---- The editor's side of the effects

// What the layer does for the cross-file effects (skill smith-lang-effects, issue #246): it tells the plugin which
// object each source is built into, so a saved source's summary lands where the build keeps it; it has the saved file
// parsed again (clangd's own `forceRebuild`, no text is sent: the editor's text is clangd's draft); when a summary is
// written it has the other open files parsed again, so their diagnostics follow; and in the background it compiles,
// one at a time, each source whose summary is missing or older than the source.
class Editor {
  public:
    explicit Editor(int serverFd) : serverFd(serverFd) {}

    // One editor message, before it goes to clangd.
    void observe(const std::string &body) {
        auto parsed = llvm::json::parse(body);
        if (!parsed) {
            llvm::consumeError(parsed.takeError());
            return;
        }
        Object *message = parsed->getAsObject();
        if (message == nullptr) return;
        auto method = message->getString("method");
        Object *params = message->getObject("params");
        if (!method || params == nullptr) return;
        if (*method == "initialize") {
            initialize(*params);
            return;
        }
        Object *document = params->getObject("textDocument");
        if (document == nullptr) return;
        auto uri = document->getString("uri");
        if (!uri) return;
        std::lock_guard<std::mutex> guard(lock);
        if (*method == "textDocument/didOpen") {
            versions[uri->str()] = document->getInteger("version").value_or(0);
        } else if (*method == "textDocument/didChange") {
            versions[uri->str()] = document->getInteger("version").value_or(versions[uri->str()]);
        } else if (*method == "textDocument/didClose") {
            versions.erase(uri->str());
        } else if (*method == "textDocument/didSave") {
            saved.push_back(uri->str());
        }
    }

    // After the saved message went to clangd: the saved file is parsed again, and its parse writes the summary.
    void afterForward() {
        std::vector<std::string> uris;
        {
            std::lock_guard<std::mutex> guard(lock);
            uris.swap(saved);
        }
        for (const std::string &uri : uris) {
            smith_effects_editor_saved(pathOfUri(uri).c_str());
            rebuild(uri);
        }
    }

    // A summary was written (the plugin's parse of `source` did): the other open files are parsed again.
    void written(const std::string &source) {
        std::vector<std::string> uris;
        {
            std::lock_guard<std::mutex> guard(lock);
            for (const auto &entry : versions) {
                std::string path = pathOfUri(entry.first);
                char real[4096];
                if (realpath(path.c_str(), real) != nullptr) path = real;
                if (path != source) uris.push_back(entry.first);
            }
        }
        for (const std::string &uri : uris)
            rebuild(uri);
    }

    void sendLocked(const std::string &body) {
        std::lock_guard<std::mutex> guard(sending);
        sendFrame(serverFd, body);
    }

    bool forward(const std::string &body) {
        std::lock_guard<std::mutex> guard(sending);
        return sendFrame(serverFd, body);
    }

  private:
    int serverFd;
    std::mutex lock, sending;
    std::map<std::string, int64_t> versions; // the open documents
    std::vector<std::string> saved;

    // `textDocument/didChange` with no changes and `forceRebuild`: clangd keeps its draft and parses it again.
    void rebuild(const std::string &uri) {
        int64_t version = 0;
        {
            std::lock_guard<std::mutex> guard(lock);
            auto found = versions.find(uri);
            if (found == versions.end()) return;
            version = found->second;
        }
        Object document{{"uri", uri}, {"version", version}};
        Object params{{"textDocument", std::move(document)}, {"contentChanges", Array()}, {"forceRebuild", true}};
        Object message{{"jsonrpc", "2.0"}, {"method", "textDocument/didChange"}, {"params", std::move(params)}};
        sendLocked(llvm::formatv("{0}", Value(std::move(message))).str());
    }

    void initialize(Object &params) {
        std::string root;
        if (auto uri = params.getString("rootUri")) root = pathOfUri(*uri);
        if (root.empty()) {
            if (auto path = params.getString("rootPath")) root = path->str();
        }
        if (root.empty()) return;
        // Pending from this message on, not from when the thread gets to run: clangd parses the first document
        // as soon as it is opened, and a parse that comes before the flag is set judges a function whose summary
        // is not written yet as having no source (`unknown`), which is the flake of #813 on a loaded machine.
        smith_effects_editor_pending(1);
        std::thread([this, root] { background(root); }).detach();
    }

    // The compile commands of the project: each source's object goes to the plugin, and the sources whose summary is
    // missing or older are compiled, one at a time, for the summary only.
    void background(const std::string &root) {
        backgroundPass(root);
        smith_effects_editor_pending(0);
    }

    void backgroundPass(const std::string &root) {
        std::string path = root + "/.smith/compile_commands.json";
        auto buffer = llvm::MemoryBuffer::getFile(path);
        if (!buffer) return;
        auto parsed = llvm::json::parse((*buffer)->getBuffer());
        if (!parsed) {
            llvm::consumeError(parsed.takeError());
            return;
        }
        Array *entries = parsed->getAsArray();
        if (entries == nullptr) return;
        struct Job {
            std::string directory, source, object;
            std::vector<std::string> arguments;
        };
        std::vector<Job> stale;
        for (Value &entry : *entries) {
            Object *object = entry.getAsObject();
            if (object == nullptr) continue;
            auto directory = object->getString("directory");
            auto file = object->getString("file");
            auto output = object->getString("output");
            Array *arguments = object->getArray("arguments");
            if (!directory || !file || !output || arguments == nullptr) continue;
            Job job;
            job.directory = directory->str();
            job.source = file->starts_with("/") ? file->str() : job.directory + "/" + file->str();
            job.object = output->starts_with("/") ? output->str() : job.directory + "/" + output->str();
            for (Value &argument : *arguments) {
                if (auto text = argument.getAsString()) job.arguments.push_back(text->str());
            }
            smith_effects_editor_object(job.source.c_str(), job.object.c_str());
            std::string summary = root + "/" + summaryOf(job.object);
            struct stat summaryInfo = {}, sourceInfo = {};
            bool fresh = stat(summary.c_str(), &summaryInfo) == 0 && stat(job.source.c_str(), &sourceInfo) == 0 &&
                         summaryInfo.st_mtime >= sourceInfo.st_mtime;
            if (!fresh && stat(job.source.c_str(), &sourceInfo) == 0) stale.push_back(std::move(job));
        }
        if (stale.empty()) return;
        for (const Job &job : stale)
            summarize(job.directory, job.object, job.arguments);
        smith_effects_editor_pending(0); // before the files are parsed again, which judge with all the summaries
        written("");                     // every open file is judged again with all the summaries
    }

    // `.smith/effects/<the object's absolute path, % for />.fx`, the plugin's place (smith_effects_summary_path).
    static std::string summaryOf(std::string object) {
        for (char &c : object) {
            if (c == '/') c = '%';
        }
        return ".smith/effects/" + object + ".fx";
    }

    // The compile command of a source, for the summary only: no object, no depfile, `SMITH_EFFECTS_OBJECT` says whose
    // summary it is. Runs in the project's folder and waits.
    static void summarize(const std::string &directory, const std::string &object, std::vector<std::string> arguments) {
        char self[4096];
        if (!smith_platform_self_path(self, sizeof self)) return;
        std::vector<std::string> kept;
        for (size_t i = 0; i < arguments.size(); i++) {
            const std::string &argument = arguments[i];
            if (argument == "-o" || argument == "-MF" || argument == "-MT" || argument == "-MQ") {
                i += 1;
            } else if (argument != "-MD" && argument != "-MMD" && argument != "-MP") {
                kept.push_back(argument);
            }
        }
        kept.erase(std::remove(kept.begin(), kept.end(), std::string("-c")), kept.end());
        kept.push_back("-fsyntax-only");
        std::vector<char *> argv;
        for (std::string &argument : kept)
            argv.push_back(argument.data());
        argv.push_back(nullptr);
        std::string variable = "SMITH_EFFECTS_OBJECT=" + object;
        std::vector<char *> environment;
        for (char **entry = environ; *entry != nullptr; entry++)
            environment.push_back(*entry);
        environment.push_back(variable.data());
        environment.push_back(nullptr);
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
        posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
        smith_platform_spawn_chdir(&actions, directory.c_str());
        pid_t child = 0;
        if (posix_spawn(&child, self, &actions, nullptr, argv.data(), environment.data()) == 0) {
            setpriority(PRIO_PROCESS, (id_t)child, 10);
            int status = 0;
            waitpid(child, &status, 0);
        }
        posix_spawn_file_actions_destroy(&actions);
    }
};

Editor *editor = nullptr; // lives as long as the process, like the layer

void summaryWritten(const char *source) {
    if (editor != nullptr) editor->written(source);
}

// ---- The pipes

class Frames {
  public:
    explicit Frames(int fd) : fd(fd) {}

    // Whether a message can be read within `milliseconds` (one is half read, or the input has bytes or its end).
    bool ready(int milliseconds) {
        if (!pending.empty()) return true;
        struct pollfd watch = {fd, POLLIN, 0};
        return poll(&watch, 1, milliseconds) > 0;
    }

    // The body of the next message; false at the end of the input.
    bool next(std::string &body) {
        size_t end = std::string::npos;
        while ((end = pending.find("\r\n\r\n")) == std::string::npos) {
            if (!fill()) return false;
        }
        size_t at = pending.find("Content-Length:");
        size_t length = at != std::string::npos && at < end ? std::strtoul(pending.c_str() + at + 15, nullptr, 10) : 0;
        pending.erase(0, end + 4);
        while (pending.size() < length) {
            if (!fill()) return false;
        }
        body = pending.substr(0, length);
        pending.erase(0, length);
        return true;
    }

  private:
    int fd;
    std::string pending;

    bool fill() {
        char chunk[1 << 16];
        for (;;) {
            ssize_t got = read(fd, chunk, sizeof chunk);
            if (got < 0 && errno == EINTR) continue;
            if (got <= 0) return false;
            pending.append(chunk, (size_t)got);
            return true;
        }
    }
};

bool writeAll(int fd, const char *data, size_t length) {
    while (length > 0) {
        ssize_t put = write(fd, data, length);
        if (put < 0 && errno == EINTR) continue;
        if (put <= 0) return false;
        data += put;
        length -= (size_t)put;
    }
    return true;
}

bool sendFrame(int fd, const std::string &body) {
    std::string frame = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    return writeAll(fd, frame.data(), frame.size());
}

// Whether clangd is to talk the protocol on its standard input and output (and not print its version, check a file or
// speak the delimited form of the messages), and the editor has not asked for the compiler's names.
bool serves(int argc, char **argv) {
    if (const char *raw = getenv("SMITH_LSP_RAW")) {
        if (raw[0] != 0 && raw[0] != '0') return false;
    }
    for (int i = 1; i < argc; i++) {
        StringRef arg(argv[i]);
        if (arg == "-h" || arg.starts_with("--help") || arg.starts_with("--version") || arg.starts_with("--check") ||
            arg.starts_with("--input-style=delimited") || arg.starts_with("--print-")) {
            return false;
        }
    }
    return true;
}

// ---- Queries from the shell (issue #248)
//
// `smith lsp def|refs|hover|diag|symbols NAME...` and `smith lsp -` (one query a line from the standard input) are
// answered by a client of the same server the editor talks to, behind the same layer: clangd runs in this process on a
// pair of pipes, its answers come back with the names as the source writes them, and what it knows is its own index on
// disk, shared with the editor. The output is grep's shape, `path:line:col: kind: text`, one fact a line.

bool queryWord(StringRef word) {
    return word == "def" || word == "refs" || word == "hover" || word == "diag" || word == "symbols" || word == "-";
}

// The folder a query's paths are relative to.
std::string workingFolder() {
    char buffer[4096];
    return getcwd(buffer, sizeof buffer) != nullptr ? buffer : ".";
}

std::string relativePath(StringRef path) {
    static const std::string folder = workingFolder() + "/";
    return path.starts_with(folder) ? path.drop_front(folder.size()).str() : path.str();
}

std::string uriOfPath(StringRef path) {
    std::string uri = "file://";
    for (char c : path) {
        if (c == ' ' || c == '%' || c == '#' || c == '?') {
            char escaped[4];
            snprintf(escaped, sizeof escaped, "%%%02X", (unsigned)(unsigned char)c);
            uri += escaped;
        } else {
            uri += c;
        }
    }
    return uri;
}

const char *kindName(int64_t kind) {
    static const char *const names[] = {"",         "file",     "module",   "namespace",   "package",     "class",
                                        "method",   "property", "field",    "constructor", "enum",        "interface",
                                        "function", "variable", "constant", "string",      "number",      "boolean",
                                        "array",    "object",   "key",      "null",        "enum-member", "struct",
                                        "event",    "operator", "type"};
    return kind > 0 && kind < (int64_t)(sizeof names / sizeof names[0]) ? names[kind] : "symbol";
}

// What a place says: the line of the file, the way the source wrote it.
class Sources {
  public:
    std::string line(const std::string &path, int64_t number) {
        auto found = files.find(path);
        if (found == files.end()) {
            std::vector<std::string> lines;
            std::ifstream file(path);
            for (std::string text; std::getline(file, text);)
                lines.push_back(text);
            found = files.emplace(path, std::move(lines)).first;
        }
        if (number < 0 || number >= (int64_t)found->second.size()) return "";
        StringRef text = StringRef(found->second[(size_t)number]).trim();
        return text.take_front(160).str();
    }

  private:
    std::map<std::string, std::vector<std::string>> files;
};

struct Place {
    std::string path;
    int64_t line = 0;
    int64_t column = 0;
    std::string kind;
    std::string name;
    std::string lowered; // the compiler's name, as the index has it
    bool operator<(const Place &other) const {
        return std::tie(path, line, column) < std::tie(other.path, other.line, other.column);
    }
};

class Session {
  public:
    Session(Layer &layer, int toServer, int fromServer, int out)
        : layer(layer), toServer(toServer), frames(fromServer), out(out) {}

    int status = 0;

    bool start() {
        std::string root = projectRoot();
        Object workspace{{"symbol", Object{}}};
        Object text{{"hover", Object{{"contentFormat", Array{"plaintext"}}}},
                    {"documentSymbol", Object{{"hierarchicalDocumentSymbolSupport", false}}},
                    {"publishDiagnostics", Object{{"versionSupport", true}}}};
        Object capabilities{{"workspace", std::move(workspace)},
                            {"textDocument", std::move(text)},
                            {"window", Object{{"workDoneProgress", true}}}};
        Object params{
            {"processId", (int64_t)getpid()}, {"rootUri", uriOfPath(root)}, {"capabilities", std::move(capabilities)}};
        if (!request("initialize", std::move(params), 60)) return false;
        notify("initialized", Object{});
        // The server reads the project's compile commands, and starts its background index, when the first file is
        // opened: any one of the project's sources.
        std::vector<std::string> files = projectSources();
        if (!files.empty()) open(files[0]);
        waitForIndex();
        return true;
    }

    void finish() {
        request("shutdown", Value(nullptr), 5);
        notify("exit", Object{});
    }

    void run(const std::string &word, const std::vector<std::string> &operands) {
        if (word == "def") {
            for (const std::string &name : operands)
                definitions(name);
        } else if (word == "refs") {
            for (const std::string &name : operands)
                references(name);
        } else if (word == "hover") {
            for (const std::string &spot : operands)
                hover(spot);
        } else if (word == "symbols") {
            for (const std::string &file : operands)
                outline(file);
        } else if (word == "diag") {
            diagnose(operands);
        }
    }

  private:
    Layer &layer;
    int toServer;
    Frames frames;
    int out;
    int64_t nextId = 1;
    std::set<std::string> opened;
    struct Published {
        int64_t version = -1;
        Array items;
    };
    std::map<std::string, Published> diagnostics;
    bool indexBegun = false;
    bool indexEnded = false;
    std::string indexProgress; // the server's last "done/total" of the index
    Sources sources;

    enum class Read { Message, Timeout, Closed };

    static std::string projectRoot() {
        std::string folder = workingFolder();
        for (std::string here = folder; !here.empty() && here != "/"; here = here.substr(0, here.rfind('/'))) {
            if (access((here + "/.smith/compile_commands.json").c_str(), F_OK) == 0 ||
                access((here + "/compile_commands.json").c_str(), F_OK) == 0) {
                return here;
            }
            if (here.rfind('/') == 0) break;
        }
        return folder;
    }

    void say(const std::string &line) {
        std::string text = line + "\n";
        if (!writeAll(out, text.data(), text.size())) status = status != 0 ? status : 2;
    }

    bool send(Object message, bool through = true) {
        message["jsonrpc"] = "2.0";
        std::string body = llvm::formatv("{0}", Value(std::move(message))).str();
        if (through) layer.toServer(body);
        return sendFrame(toServer, body);
    }

    void notify(const char *method, Value params) {
        send(Object{{"method", method}, {"params", std::move(params)}});
    }

    // A message of the server's: its requests are answered, its notifications noted.
    void handle(Object &message) {
        auto method = message.getString("method");
        if (!method) return;
        if (Value *id = message.get("id")) {
            send(Object{{"id", *id}, {"result", nullptr}}, false); // a progress token, a registration: accepted
            return;
        }
        Object *params = message.getObject("params");
        if (params == nullptr) return;
        if (*method == "$/progress") {
            Object *value = params->getObject("value");
            auto kind = value != nullptr ? value->getString("kind") : std::nullopt;
            if (kind && *kind == "begin") {
                indexBegun = true;
                std::cerr << "smith lsp: bringing the index up to date\n";
            }
            if (kind && *kind == "report" && value != nullptr)
                indexProgress = value->getString("message").value_or("").str();
            if (kind && *kind == "end") indexEnded = true;
        } else if (*method == "textDocument/publishDiagnostics") {
            auto uri = params->getString("uri");
            Array *items = params->getArray("diagnostics");
            if (!uri || items == nullptr) return;
            Published &entry = diagnostics[uri->str()];
            entry.version = params->getInteger("version").value_or(-1);
            entry.items = *items;
        }
    }

    Read read(Object &message, int milliseconds) {
        if (!frames.ready(milliseconds)) return Read::Timeout;
        std::string body;
        if (!frames.next(body)) return Read::Closed;
        auto parsed = llvm::json::parse(layer.toEditor(std::move(body)));
        if (!parsed) {
            llvm::consumeError(parsed.takeError());
            return Read::Message;
        }
        Object *object = parsed->getAsObject();
        if (object == nullptr) return Read::Message;
        message = std::move(*object);
        handle(message);
        return Read::Message;
    }

    // The answer of a request, or nothing when the server did not give one in time or refused it.
    std::optional<Value> request(const char *method, Value params, int seconds, bool through = true) {
        int64_t id = nextId++;
        if (!send(Object{{"id", id}, {"method", method}, {"params", std::move(params)}}, through)) return std::nullopt;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
        while (std::chrono::steady_clock::now() < deadline) {
            Object message;
            Read got = read(message, 100);
            if (got == Read::Closed) return std::nullopt;
            if (got == Read::Timeout || message.get("method") != nullptr) continue;
            Value *answered = message.get("id");
            if (answered == nullptr || answered->getAsInteger().value_or(-1) != id) continue;
            if (message.get("error") != nullptr) return std::nullopt;
            Value *result = message.get("result");
            return result != nullptr ? std::optional<Value>(std::move(*result)) : std::optional<Value>(nullptr);
        }
        return std::nullopt;
    }

    // The index is up to date when the server's background indexing has begun and ended; it is not begun when there is
    // nothing to index, which the server does not announce, so a moment with no beginning is "nothing".
    void waitForIndex() {
        auto begin = std::chrono::steady_clock::now();
        auto told = begin;
        for (;;) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = now - begin;
            if (indexEnded) return;
            if (elapsed > std::chrono::minutes(30)) { // never an answer from an incomplete index without saying so
                std::cerr << "smith lsp: the index is not finished (" << indexProgress
                          << "): the answer may be incomplete\n";
                if (status == 0) status = 1;
                return;
            }
            if (!indexBegun && elapsed > std::chrono::seconds(3)) return;
            if (indexBegun && now - told > std::chrono::seconds(10)) {
                told = now;
                std::cerr << "smith lsp: indexing " << indexProgress << "\n";
            }
            Object message;
            if (read(message, 100) == Read::Closed) return;
        }
    }

    bool open(const std::string &path, bool send = true) {
        std::string uri = uriOfPath(path);
        if (opened.count(uri) != 0) return true;
        std::ifstream file(path);
        if (!file) return false;
        if (!send) return true;
        std::stringstream text;
        text << file.rdbuf();
        notify("textDocument/didOpen",
               Object{{"textDocument",
                       Object{{"uri", uri}, {"languageId", "c"}, {"version", (int64_t)1}, {"text", text.str()}}}});
        opened.insert(uri);
        return true;
    }

    static std::string absolute(const std::string &path) {
        if (!path.empty() && path[0] == '/') return path;
        return workingFolder() + "/" + (StringRef(path).starts_with("./") ? path.substr(2) : path);
    }

    static bool endsWithName(StringRef written, StringRef name) {
        if (written == name) return true;
        return written.size() > name.size() + 2 && written.ends_with(name) &&
               written.substr(written.size() - name.size() - 2, 2) == "::";
    }

    static Place placeOf(Object &symbol) {
        Place place;
        place.name = symbol.getString("name").value_or("").str();
        place.kind = kindName(symbol.getInteger("kind").value_or(0));
        Object *location = symbol.getObject("location");
        if (location == nullptr) return place;
        place.path = pathOfUri(location->getString("uri").value_or(""));
        Object *range = location->getObject("range");
        Object *start = range != nullptr ? range->getObject("start") : nullptr;
        if (start != nullptr) {
            place.line = start->getInteger("line").value_or(0);
            place.column = start->getInteger("character").value_or(0);
        }
        return place;
    }

    // Waits until the server has read the file (its diagnostics are published): the names the lowering made of it are
    // known then, and `smith::namesOf` has them.
    void awaitParsed(const std::string &path) {
        std::string uri = uriOfPath(path);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (diagnostics.find(uri) == diagnostics.end() && std::chrono::steady_clock::now() < deadline) {
            Object message;
            if (read(message, 100) == Read::Closed) return;
        }
        if (diagnostics.find(uri) == diagnostics.end()) {
            std::cerr << "smith lsp: " << relativePath(path)
                      << ": the server did not read it in 30 s; what it holds is missing\n";
            diagnostics[uri]; // said once
            if (status == 0) status = 1;
        }
    }

    // A name of a symbol in a .smith file as the source writes it, with its module: `std::arena::push`.
    std::string qualified(const Place &place) {
        if (!open(place.path)) return "";
        awaitParsed(place.path);
        std::shared_ptr<const smith::FileNames> names = smith::namesOf(place.path);
        const smith::WrittenName *written = names ? names->find(place.name) : nullptr;
        if (written == nullptr) return "";
        return written->module.empty() ? written->written : written->module + "::" + written->member;
    }

    // The places a name is defined: by the name as the source writes it (a bare name, `module::name` or the whole
    // path), or by the C name the compiler calls it. The index is asked for the module path and the name joined with
    // `_`, which is the compiler's name, and the names of the .smith files that hold the answers say what is written.
    std::vector<Place> find(const std::string &name, std::vector<Place> *headers = nullptr) {
        std::set<Place> found;
        std::map<std::string, Place> declared; // the generated header's declarations: where C reaches the name
        std::vector<Place> instances;          // a generic's instances (`<name>__f32`), where its calls are
        std::string joined = name;
        for (size_t at; (at = joined.find("::")) != std::string::npos;)
            joined.replace(at, 2, "_");
        // A function with a C name (`[[std::c_name("smith_arena_push")]] push_raw`) is in the index under the C name,
        // which the written name does not spell: when the joined name finds nothing, the module's own name is asked
        // for and every .smith symbol it brings is judged by what the source writes.
        std::string module = StringRef(name).rsplit("::").first.str();
        for (size_t at; (at = module.rfind("::")) != std::string::npos;)
            module = module.substr(at + 2);
        for (int pass = 0; pass < 2 && found.empty(); pass++) {
            if (pass == 1 && (module.empty() || module == name)) break;
            auto answer = request("workspace/symbol", Object{{"query", pass == 0 ? joined : module}}, 120, false);
            Array *list = answer ? answer->getAsArray() : nullptr;
            if (list == nullptr) return {};
            for (Value &entry : *list) {
                Object *symbol = entry.getAsObject();
                if (symbol == nullptr) continue;
                Place place = placeOf(*symbol);
                place.lowered = place.name;
                if (place.path.empty()) continue;
                if (place.path.find("/.smith/include/") != std::string::npos) {
                    declared.emplace(place.lowered, place);
                    continue;
                }
                if (place.lowered.find("__") != std::string::npos) instances.push_back(place);
                if (place.name == name) {
                    found.insert(place); // a C name
                    continue;
                }
                bool smith = StringRef(place.path).ends_with(".smith");
                bool candidate =
                    pass == 1 || place.name.find(StringRef(name).rsplit("::").second.str()) != std::string::npos;
                if (!smith || !candidate) continue;
                std::string written = qualified(place);
                if (written.empty() || !endsWithName(written, name)) continue;
                place.name = written;
                found.insert(place);
            }
        }
        if (headers != nullptr) {
            for (const Place &place : found) {
                auto header = declared.find(place.lowered);
                if (header != declared.end()) headers->push_back(header->second);
                for (const Place &instance : instances) {
                    if (StringRef(instance.lowered).starts_with(place.lowered + "__")) headers->push_back(instance);
                }
            }
        }
        return std::vector<Place>(found.begin(), found.end());
    }

    void definitions(const std::string &name) {
        std::vector<Place> places = find(name);
        for (const Place &place : places) {
            say(relativePath(place.path) + ":" + std::to_string(place.line + 1) + ":" +
                std::to_string(place.column + 1) + ": " + place.kind + ": " + place.name + ": " +
                sources.line(place.path, place.line));
        }
        if (places.empty()) status = status != 0 ? status : 1;
    }

    // The calls of a generic: the compiler sees an instance where one is written, so the index has the instance, not
    // the call. The lowering knows each call (shim/names.h, CallSite) of the files it parsed: the files that write the
    // name are read, and each call that says it is of this generic is a reference.
    void genericCalls(const Place &generic, std::set<Place> &found) {
        StringRef member = StringRef(generic.name).rsplit("::").second;
        for (const std::string &path : projectSources()) {
            if (!StringRef(path).ends_with(".smith") || !open(path, false)) continue;
            std::ifstream file(path);
            std::stringstream text;
            text << file.rdbuf();
            if (text.str().find(member.str() + "(") == std::string::npos) continue;
            open(path);
            awaitParsed(path);
            std::shared_ptr<const smith::FileNames> names = smith::namesOf(path);
            if (!names) continue;
            for (const smith::CallSite &site : names->calls) {
                if (site.file != generic.path || (int64_t)site.definition_line != generic.line) continue;
                Place use;
                use.path = path;
                use.line = site.line;
                use.column = site.column;
                found.insert(use);
            }
        }
    }

    void references(const std::string &name) {
        std::set<Place> found;
        std::vector<Place> headers;
        std::vector<Place> places = find(name, &headers);
        for (const Place &place : places) {
            if (StringRef(place.path).ends_with(".smith")) genericCalls(place, found);
        }
        places.insert(places.end(), headers.begin(), headers.end()); // the declaration C code uses is its own symbol
        for (const Place &place : places) {
            if (!open(place.path)) continue;
            Object where{{"textDocument", Object{{"uri", uriOfPath(place.path)}}},
                         {"position", Object{{"line", place.line}, {"character", place.column}}},
                         {"context", Object{{"includeDeclaration", true}}}};
            auto answer = request("textDocument/references", std::move(where), 120);
            Array *list = answer ? answer->getAsArray() : nullptr;
            if (list == nullptr) continue;
            for (Value &entry : *list) {
                Object *location = entry.getAsObject();
                if (location == nullptr) continue;
                Place use;
                use.path = pathOfUri(location->getString("uri").value_or(""));
                if (Object *range = location->getObject("range"))
                    if (Object *start = range->getObject("start")) {
                        use.line = start->getInteger("line").value_or(0);
                        use.column = start->getInteger("character").value_or(0);
                    }
                if (!use.path.empty() && use.path.find("/.smith/include/") == std::string::npos) found.insert(use);
            }
        }
        for (const Place &use : found) {
            say(relativePath(use.path) + ":" + std::to_string(use.line + 1) + ":" + std::to_string(use.column + 1) +
                ": ref: " + sources.line(use.path, use.line));
        }
        if (found.empty()) status = status != 0 ? status : 1;
    }

    // `FILE:LINE:COL`: the file and the place, from 1.
    static bool spot(const std::string &text, std::string &file, int64_t &line, int64_t &column) {
        size_t last = text.rfind(':');
        size_t before = last == std::string::npos || last == 0 ? std::string::npos : text.rfind(':', last - 1);
        if (before == std::string::npos) return false;
        file = text.substr(0, before);
        line = std::strtoll(text.c_str() + before + 1, nullptr, 10) - 1;
        column = std::strtoll(text.c_str() + last + 1, nullptr, 10) - 1;
        return line >= 0 && column >= 0;
    }

    static void flatten(Value &contents, std::vector<std::string> &lines) {
        std::string text;
        if (auto plain = contents.getAsString()) {
            text = plain->str();
        } else if (Object *markup = contents.getAsObject()) {
            text = markup->getString("value").value_or("").str();
        } else if (Array *parts = contents.getAsArray()) {
            for (Value &part : *parts)
                flatten(part, lines);
            return;
        }
        std::stringstream stream(text);
        for (std::string line; std::getline(stream, line);) {
            StringRef trimmed = StringRef(line).trim();
            if (trimmed.empty() || trimmed.starts_with("```") || trimmed == "---") continue;
            lines.push_back(trimmed.str());
        }
    }

    void hover(const std::string &where) {
        std::string file;
        int64_t line = 0;
        int64_t column = 0;
        if (!spot(where, file, line, column)) {
            std::cerr << "smith lsp hover: `" << where << "` is not FILE:LINE:COL\n";
            status = 2;
            return;
        }
        std::string path = absolute(file);
        if (!open(path)) {
            std::cerr << "smith lsp hover: cannot read " << file << "\n";
            status = 2;
            return;
        }
        Object params{{"textDocument", Object{{"uri", uriOfPath(path)}}},
                      {"position", Object{{"line", line}, {"character", column}}}};
        auto answer = request("textDocument/hover", std::move(params), 120);
        Object *object = answer ? answer->getAsObject() : nullptr;
        Value *contents = object != nullptr ? object->get("contents") : nullptr;
        std::vector<std::string> lines;
        if (contents != nullptr) flatten(*contents, lines);
        for (const std::string &text : lines)
            say(relativePath(path) + ":" + std::to_string(line + 1) + ":" + std::to_string(column + 1) +
                ": hover: " + text);
        if (lines.empty()) status = status != 0 ? status : 1;
    }

    void outlineOf(Array &list, const std::string &path) {
        for (Value &entry : list) {
            Object *symbol = entry.getAsObject();
            if (symbol == nullptr) continue;
            Place place = placeOf(*symbol);
            if (symbol->getObject("location") == nullptr) { // a tree: the range is in the symbol itself
                place.path = path;
                if (Object *range = symbol->getObject("selectionRange"))
                    if (Object *start = range->getObject("start")) {
                        place.line = start->getInteger("line").value_or(0);
                        place.column = start->getInteger("character").value_or(0);
                    }
            }
            std::string detail = symbol->getString("detail").value_or("").str();
            say(relativePath(path) + ":" + std::to_string(place.line + 1) + ":" + std::to_string(place.column + 1) +
                ": " + place.kind + ": " + place.name + (detail.empty() ? "" : "  " + detail));
            if (Array *children = symbol->getArray("children")) outlineOf(*children, path);
        }
    }

    void outline(const std::string &file) {
        std::string path = absolute(file);
        if (!open(path)) {
            std::cerr << "smith lsp symbols: cannot read " << file << "\n";
            status = 2;
            return;
        }
        auto answer =
            request("textDocument/documentSymbol", Object{{"textDocument", Object{{"uri", uriOfPath(path)}}}}, 120);
        Array *list = answer ? answer->getAsArray() : nullptr;
        if (list != nullptr) outlineOf(*list, path);
    }

    // The sources of the project's compile commands, for `diag` with no files.
    static std::vector<std::string> projectSources() {
        std::vector<std::string> files;
        std::string root = projectRoot();
        for (const char *database : {"/.smith/compile_commands.json", "/compile_commands.json"}) {
            auto buffer = llvm::MemoryBuffer::getFile(root + database);
            if (!buffer) continue;
            auto parsed = llvm::json::parse((*buffer)->getBuffer());
            if (!parsed) {
                llvm::consumeError(parsed.takeError());
                continue;
            }
            Array *entries = parsed->getAsArray();
            if (entries == nullptr) continue;
            for (Value &entry : *entries) {
                Object *command = entry.getAsObject();
                if (command == nullptr) continue;
                std::string file = command->getString("file").value_or("").str();
                if (file.empty()) continue;
                if (file[0] != '/') file = command->getString("directory").value_or(root).str() + "/" + file;
                if (file.find("/.smith/include/") == std::string::npos) files.push_back(file);
            }
            break;
        }
        std::sort(files.begin(), files.end());
        files.erase(std::unique(files.begin(), files.end()), files.end());
        return files;
    }

    void diagnose(const std::vector<std::string> &operands) {
        std::vector<std::string> files;
        for (const std::string &file : operands)
            files.push_back(absolute(file));
        if (files.empty()) files = projectSources();
        for (const std::string &path : files) {
            if (!open(path)) {
                std::cerr << "smith lsp diag: cannot read " << relativePath(path) << "\n";
                status = 2;
                continue;
            }
            std::string uri = uriOfPath(path);
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
            while (diagnostics.find(uri) == diagnostics.end() && std::chrono::steady_clock::now() < deadline) {
                Object message;
                if (read(message, 100) == Read::Closed) break;
            }
            Published &published = diagnostics[uri];
            for (Value &entry : published.items) {
                Object *item = entry.getAsObject();
                if (item == nullptr) continue;
                int64_t severity = item->getInteger("severity").value_or(1);
                static const char *const names[] = {"", "error", "warning", "note", "hint"};
                int64_t line = 0;
                int64_t column = 0;
                if (Object *range = item->getObject("range"))
                    if (Object *start = range->getObject("start")) {
                        line = start->getInteger("line").value_or(0);
                        column = start->getInteger("character").value_or(0);
                    }
                std::string message = item->getString("message").value_or("").str();
                std::replace(message.begin(), message.end(), '\n', ' ');
                say(relativePath(path) + ":" + std::to_string(line + 1) + ":" + std::to_string(column + 1) + ": " +
                    names[severity >= 1 && severity <= 4 ? severity : 1] + ": " + message);
                if (severity == 1) status = 1;
            }
        }
    }
};

// The words of a line of a batch.
std::vector<std::string> wordsOf(const std::string &line) {
    std::vector<std::string> words;
    std::stringstream stream(line);
    for (std::string word; stream >> word;)
        words.push_back(word);
    return words;
}

// Runs the queries of the command line, or of the standard input for `-`, against the server `server` on pipes behind
// the layer. Returns the status: 0, 1 when a query found nothing or a diagnostic is an error, 2 for a bad query.
int runQueries(int argc, char **argv, int (*server)(int, char **)) {
    std::vector<std::string> operands;
    std::vector<char *> flags = {argv[0]};
    bool verbose = false;
    for (int i = 1; i < argc; i++) {
        StringRef arg(argv[i]);
        if (arg == "--verbose") {
            verbose = true;
        } else if (arg.starts_with("--")) {
            flags.push_back(argv[i]);
        } else {
            operands.push_back(argv[i]);
        }
    }
    static char logging[] = "--log=error";
    static char verbosely[] = "--log=verbose";
    static char unlimited[] = "--limit-results=0"; // a bare name is every module's: all of them, not a hundred
    static char priority[] = "--background-index-priority=normal"; // the query waits for the index: not at low priority
    flags.push_back(verbose ? verbosely : logging);
    flags.push_back(unlimited);
    flags.push_back(priority);
    flags.push_back(nullptr);
    std::string word = operands.empty() ? "" : operands[0];
    operands.erase(operands.begin(), operands.empty() ? operands.begin() : operands.begin() + 1);
    int fromUser = dup(0);
    int toUser = dup(1);
    int toServer[2];
    int fromServer[2];
    if (fromUser < 0 || toUser < 0 || pipe(toServer) != 0 || pipe(fromServer) != 0) return 2;
    dup2(toServer[0], 0);
    close(toServer[0]);
    dup2(fromServer[1], 1);
    close(fromServer[1]);
    Layer *layer = new Layer; // as long as the process, like the editor's
    std::atomic<int> status{0};
    std::thread client([&] {
        Session session(*layer, toServer[1], fromServer[0], toUser);
        if (!session.start()) {
            std::cerr << "smith lsp: the language server did not start\n";
            status = 2;
        } else {
            if (word == "-") {
                std::string text;
                char chunk[1 << 12];
                for (ssize_t got; (got = read(fromUser, chunk, sizeof chunk)) > 0;)
                    text.append(chunk, (size_t)got);
                std::stringstream lines(text);
                for (std::string line; std::getline(lines, line);) {
                    std::vector<std::string> words = wordsOf(line);
                    if (words.empty() || words[0][0] == '#') continue;
                    if (!queryWord(words[0]) || words[0] == "-") {
                        std::cerr << "smith lsp -: `" << line << "` is not a query (def, refs, hover, diag, symbols)\n";
                        session.status = 2;
                        continue;
                    }
                    session.run(words[0], std::vector<std::string>(words.begin() + 1, words.end()));
                }
            } else {
                session.run(word, operands);
            }
            session.finish();
            status = session.status;
        }
        close(toServer[1]); // clangd reads the end of its input
    });
    int ended = server((int)flags.size() - 1, flags.data());
    llvm::outs().flush();
    client.join();
    return status != 0 ? status.load() : ended;
}

} // namespace

// Runs `server` (clangd's main) with its input and output behind the layer. Returns its status.
extern "C" int smith_lsp_run(int argc, char **argv, int (*server)(int, char **)) {
    smith_effects_editor_start(summaryWritten);
    if (argc > 1 && queryWord(argv[1])) return runQueries(argc, argv, server);
    if (!serves(argc, argv)) return server(argc, argv);
    int fromEditor = dup(0);
    int toEditor = dup(1);
    int toServer[2];
    int fromServer[2];
    if (fromEditor < 0 || toEditor < 0 || pipe(toServer) != 0 || pipe(fromServer) != 0) return server(argc, argv);
    dup2(toServer[0], 0);
    close(toServer[0]);
    dup2(fromServer[1], 1);
    close(fromServer[1]);
    Layer *layer = new Layer; // lives as long as the process: the reading of the editor's messages can outlast clangd
    int serverInput = toServer[1];
    editor = new Editor(serverInput);
    std::thread([layer, fromEditor, serverInput] {
        Frames frames(fromEditor);
        std::string body;
        while (frames.next(body)) {
            layer->toServer(body);
            editor->observe(body);
            if (!editor->forward(body)) break;
            editor->afterForward();
        }
        close(serverInput); // the editor is gone: clangd reads the end of its input
    }).detach();
    std::thread answers([layer, outgoing = fromServer[0], toEditor] {
        Frames frames(outgoing);
        std::string body;
        while (frames.next(body)) {
            if (!sendFrame(toEditor, layer->toEditor(std::move(body)))) break;
        }
    });
    int status = server(argc, argv);
    llvm::outs().flush();
    close(1); // clangd's output ends: what is left in the pipe is sent, then the thread ends
    answers.join();
    return status;
}
