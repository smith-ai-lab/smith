// The C++ that the language server's layer (runtime/lsp*.smith) needs: a child started in a folder through the platform
// layer (issue #559), and a C view of the names the lowering made, which shim/names.h keeps as std types (issue #560).
// The layer's decisions are in .smith; this file only converts.
#include "../runtime/lsp_edge.h"
#include "names.h"
#include "platform.h"

#include <cstring>
#include <fcntl.h>
#include <memory>
#include <spawn.h>
#include <string>
#include <sys/resource.h>
#include <sys/wait.h>
#include <vector>

extern char **environ;

extern "C" void smith_lsp_summarize(const char *directory, const char *object, const char *const *arguments,
                                    size_t count) {
    char self[4096];
    if (!smith_platform_self_path(self, sizeof self)) return;
    std::vector<char *> argv;
    for (size_t i = 0; i < count; i++)
        argv.push_back(const_cast<char *>(arguments[i]));
    argv.push_back(nullptr);
    std::string variable = std::string("SMITH_EFFECTS_OBJECT=") + object;
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
    smith_platform_spawn_chdir(&actions, directory);
    pid_t child = 0;
    if (posix_spawn(&child, self, &actions, nullptr, argv.data(), environment.data()) == 0) {
        setpriority(PRIO_PROCESS, static_cast<id_t>(child), 10);
        int status = 0;
        waitpid(child, &status, 0);
    }
    posix_spawn_file_actions_destroy(&actions);
}

namespace {

SmithLspText textOf(const std::string &text) {
    return SmithLspText{text.data(), text.size()};
}

} // namespace

struct SmithLspNames {
    std::shared_ptr<const smith::FileNames> names;
};

struct SmithLspQualifier {
    smith::Qualifier qualifier;
};

extern "C" SmithLspNames *smith_lsp_names_open(const char *path) {
    std::shared_ptr<const smith::FileNames> names = smith::namesOf(path);
    if (!names) return nullptr;
    return new SmithLspNames{std::move(names)};
}

extern "C" void smith_lsp_names_close(SmithLspNames *names) {
    delete names;
}

extern "C" bool smith_lsp_names_find(const SmithLspNames *names, SmithLspText lowered, SmithLspWritten *found) {
    const smith::WrittenName *name = names->names->find(llvm::StringRef(lowered.data, lowered.length));
    if (name == nullptr) return false;
    found->written = textOf(name->written);
    found->member = textOf(name->member);
    found->module = textOf(name->module);
    return true;
}

extern "C" size_t smith_lsp_names_call_count(const SmithLspNames *names) {
    return names->names->calls.size();
}

extern "C" bool smith_lsp_names_call(const SmithLspNames *names, size_t index, SmithLspCall *call) {
    if (index >= names->names->calls.size()) return false;
    const smith::CallSite &site = names->names->calls[index];
    call->line = site.line;
    call->column = site.column;
    call->length = site.length;
    call->hover = textOf(site.hover);
    call->file = textOf(site.file);
    call->definition_line = site.definition_line;
    call->definition_column = site.definition_column;
    return true;
}

extern "C" SmithLspQualifier *smith_lsp_qualifier_open(const char *path) {
    smith::Qualifier qualifier = smith::qualifierOf(path);
    if (!qualifier.present) return nullptr;
    return new SmithLspQualifier{std::move(qualifier)};
}

extern "C" void smith_lsp_qualifier_close(SmithLspQualifier *qualifier) {
    delete qualifier;
}

extern "C" bool smith_lsp_qualifier_is_c(const SmithLspQualifier *qualifier) {
    return qualifier->qualifier.is_c;
}

extern "C" uint32_t smith_lsp_qualifier_line(const SmithLspQualifier *qualifier) {
    return qualifier->qualifier.line;
}

extern "C" uint32_t smith_lsp_qualifier_column(const SmithLspQualifier *qualifier) {
    return qualifier->qualifier.column;
}

extern "C" SmithLspText smith_lsp_qualifier_module(const SmithLspQualifier *qualifier) {
    return textOf(qualifier->qualifier.module);
}

extern "C" SmithLspText smith_lsp_qualifier_partial(const SmithLspQualifier *qualifier) {
    return textOf(qualifier->qualifier.partial);
}
