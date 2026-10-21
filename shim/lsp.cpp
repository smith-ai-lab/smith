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

#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

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
        if (result == nullptr || result->kind() == Value::Null) return body;
        answer(request, *result);
        return llvm::formatv("{0}", *parsed).str();
    }

  private:
    std::mutex lock;
    std::map<std::string, Request> pending;

    static std::string key(const Value &id) {
        return llvm::formatv("{0}", id).str();
    }

    static bool wanted(StringRef method) {
        static const char *const methods[] = {"textDocument/hover",          "textDocument/completion",
                                              "textDocument/documentSymbol", "textDocument/signatureHelp",
                                              "textDocument/inlayHint",      "workspace/symbol",
                                              "textDocument/references",     "textDocument/rename"};
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
                if (Value *contents = object->get("contents")) rewriteText(*contents, *names, Style::Written);
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
                      request.character <= (int64_t)(qualifier.column + qualifier.partial.size());
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

// ---- The pipes

class Frames {
  public:
    explicit Frames(int fd) : fd(fd) {}

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

} // namespace

// Runs `server` (clangd's main) with its input and output behind the layer. Returns its status.
extern "C" int smith_lsp_run(int argc, char **argv, int (*server)(int, char **)) {
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
    std::thread([layer, fromEditor, serverInput] {
        Frames frames(fromEditor);
        std::string body;
        while (frames.next(body)) {
            layer->toServer(body);
            if (!sendFrame(serverInput, body)) break;
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
