// The clang side of .smith files: the hook that gives the lowering (runtime/lang.c) the tokens of a .smith file, after
// macro expansion and before the parser, puts back what it emits, and answers its questions from the parser's tables.
//
// The hook itself is a patch to clang (patches/clang-token-hook.patch): clang::TokenHook, offered each token that
// leaves Preprocessor::Lex. Everything here is smith's. It stays small and C++ only because clang's API is: the logic
// is in C, in the lowering.
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/Sema.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"

#include "../runtime/lang.h"

#include <deque>
#include <string>
#include <vector>

using namespace clang;

namespace {

// One thing to put back in front of the parser, in order: tokens, or a header to include.
struct Action {
    bool include = false;
    bool angled = false;
    std::vector<Token> tokens;
    std::string header;
    SourceLocation at;
};

class SmithHook final : public TokenHook {
  public:
    explicit SmithHook(Preprocessor &pp) : PP(pp), SM(pp.getSourceManager()), MainFile(SM.getMainFileID()) {
        lang = smith_lang_new();
        host.context = this;
        host.next = &SmithHook::nextToken;
        host.emit = &SmithHook::emitToken;
        host.include = &SmithHook::includeHeader;
        host.error = &SmithHook::reportError;
        host.watch = &SmithHook::watchName;
        host.watch_mode = &SmithHook::watchMode;
        host.previous = &SmithHook::previousToken;
        host.declared = &SmithHook::declaredInFile;
        host.watch_imports = &SmithHook::watchImports;
        host.lookup = &SmithHook::lookupName;
        host.type_info = &SmithHook::typeInfo;
        host.field = &SmithHook::fieldOf;
        host.read_header = &SmithHook::readHeader;
        if (OptionalFileEntryRef entry = SM.getFileEntryRefForID(MainFile)) {
            mainName = entry->getName().str();
        }
        host.main_file = mainName.c_str();
        diagnostic = PP.getDiagnostics().getCustomDiagID(DiagnosticsEngine::Error, "%0");
        smith_lang_watch_basics(&host);
        PP.addPPCallbacks(std::make_unique<Macros>(*this));
    }

    void semaCreated(Sema &s) override {
        sema = &s;
    }
    void semaDestroyed() override {
        sema = nullptr;
    }

    bool transform(Preprocessor &, Token &tok) override {
        if (!spelledInMain(tok.getLocation())) {
            if (tok.is(tok::eof)) return feed(tok);
            return false;
        }
        return feed(tok);
    }

  private:
    Preprocessor &PP;
    SourceManager &SM;
    FileID MainFile;
    Sema *sema = nullptr;
    SmithLang *lang;
    SmithHost host{};
    std::string mainName;
    unsigned diagnostic = 0;
    // Per call.
    std::vector<Token> pool; // every token the lowering was given, by handle
    std::vector<Token> out;
    std::vector<Action> actions;
    llvm::BumpPtrAllocator texts;
    llvm::DenseSet<unsigned> importFiles; // the FileIDs of the buffers that stand for `import <h>;`
    std::string fieldName;
    std::string fieldType;
    std::string headerBuffer;
    std::unique_ptr<llvm::MemoryBuffer> headerFile;

    bool spelledInMain(SourceLocation loc) const {
        if (loc.isInvalid()) return false;
        if (loc.isFileID()) return SM.getFileID(loc) == MainFile;
        return SM.getFileID(SM.getSpellingLoc(loc)) == MainFile;
    }

    // Whether a file was brought in by `import <h>;`: its include chain passes through one of our buffers.
    bool importOwned(FileID id) const {
        for (int depth = 0; depth < 64 && id.isValid(); depth++) { // include chains are short
            if (importFiles.contains(id.getHashValue())) return true;
            id = SM.getFileID(SM.getIncludeLoc(id));
        }
        return false;
    }

    // A macro of an imported header used where the source names it with no `c::` is an error in a module.
    void macroUsed(const MacroDefinition &definition, SourceRange range) {
        SourceLocation at = range.getBegin();
        const MacroInfo *macro = definition.getMacroInfo();
        if (!smith_lang_is_module(lang) || !at.isFileID() || SM.getFileID(at) != MainFile || macro == nullptr ||
            macro->isBuiltinMacro()) {
            return;
        }
        if (!importOwned(SM.getFileID(macro->getDefinitionLoc())) ||
            smith_lang_c_use_allowed(lang, at.getRawEncoding())) {
            return;
        }
        bool invalid = false;
        const char *start = SM.getCharacterData(at, &invalid);
        if (invalid) return;
        StringRef name(start, Lexer::MeasureTokenLength(at, SM, PP.getLangOpts()));
        std::string message = "`" + name.str() + "` is a C macro; write `c::" + name.str() + "`";
        PP.getDiagnostics().Report(at, diagnostic) << message;
    }

    class Macros final : public PPCallbacks {
      public:
        explicit Macros(SmithHook &hook) : hook(hook) {}
        void MacroExpands(const Token &, const MacroDefinition &definition, SourceRange range,
                          const MacroArgs *) override {
            hook.macroUsed(definition, range);
        }

      private:
        SmithHook &hook;
    };

    // Every name an imported header declared is watched, so a bare use of one in a module is offered to the lowering.
    static void watchImports(void *context) {
        auto *self = static_cast<SmithHook *>(context);
        if (self->sema == nullptr) return;
        for (auto &entry : self->PP.getIdentifierTable()) {
            IdentifierInfo *name = entry.second;
            if (name->getFETokenInfo() == nullptr) continue;
            for (auto at = self->sema->IdResolver.begin(name), end = self->sema->IdResolver.end(); at != end; ++at) {
                if (self->importOwned(self->SM.getFileID(self->SM.getFileLoc((*at)->getLocation())))) {
                    name->setHookWatched(true);
                    break;
                }
            }
        }
    }

    const char *keep(StringRef text) {
        char *copy = texts.Allocate<char>(text.size() + 1);
        std::memcpy(copy, text.data(), text.size());
        copy[text.size()] = 0;
        return copy;
    }

    SmithToken convert(const Token &tok, uint32_t handle) {
        SmithToken out{};
        out.handle = handle;
        out.location = tok.getLocation().getRawEncoding();
        out.line_start = tok.isAtStartOfLine();
        out.space_before = tok.hasLeadingSpace();
        out.foreign = tok.isNot(tok::eof) && !spelledInMain(tok.getLocation());
        if (tok.is(tok::eof)) {
            out.kind = SmithTokenKind_Eof;
            return out;
        }
        if (tok.isAnnotation()) {
            out.kind = SmithTokenKind_Punctuator;
            out.text = "";
            return out;
        }
        if (const char *punctuator = tok::getPunctuatorSpelling(tok.getKind())) {
            out.kind = SmithTokenKind_Punctuator;
            out.text = punctuator;
            out.length = punctuator[1] == 0 ? 1 : punctuator[2] == 0 ? 2 : 3; // spellings of 1 to 3 characters
            return out;
        }
        if (tok.isLiteral() || tok.is(tok::raw_identifier) || tok.getIdentifierInfo() != nullptr ||
            tok::getKeywordSpelling(tok.getKind()) != nullptr) {
            // Identifiers and keywords are their IdentifierInfo's name; a literal is read where it is written, unless
            // it has line splices or trigraphs to clean.
            if (const IdentifierInfo *info = tok.is(tok::raw_identifier) ? nullptr : tok.getIdentifierInfo()) {
                out.text = info->getNameStart();
                out.length = info->getLength();
            } else if (tok.isLiteral() && tok.getLiteralData() != nullptr && !tok.needsCleaning()) {
                out.text = tok.getLiteralData();
                out.length = tok.getLength();
            } else {
                std::string spelling = PP.getSpelling(tok);
                out.text = keep(spelling);
                out.length = spelling.size();
            }
            out.kind = tok.is(tok::numeric_constant) ? SmithTokenKind_Number
                       : tok.isOneOf(tok::char_constant, tok::wide_char_constant, tok::utf8_char_constant,
                                     tok::utf16_char_constant, tok::utf32_char_constant)
                           ? SmithTokenKind_Character
                       : tok.isLiteral() ? SmithTokenKind_String
                                         : SmithTokenKind_Identifier;
            return out;
        }
        out.kind = SmithTokenKind_Punctuator;
        out.text = "";
        return out;
    }

    bool feed(Token &tok) {
        if (!smith_lang_holds(lang)) pool.clear(); // held tokens are re-emitted by handle
        out.clear();
        actions.clear();
        texts.Reset();
        pool.push_back(tok);
        SmithToken first = convert(tok, pool.size() - 1);
        bool consumed = smith_lang_feed(lang, &host, &first);
        if (!consumed) return false;
        if (actions.empty() && out.size() == 1 && replaceInPlace(tok, out[0])) return false;
        flush();
        return true;
    }

    // The type a sized type name stands for, as the compiler's own: u32 is the target's unsigned int.
    QualType sizedType(StringRef macro) {
        ASTContext &ast = sema->getASTContext();
        const TargetInfo &target = ast.getTargetInfo();
        if (macro == "__SIZE_TYPE__") return ast.getSizeType();
        if (macro == "__PTRDIFF_TYPE__") return ast.getPointerDiffType();
        if (!macro.consume_front("__") || !macro.consume_back("_TYPE__")) return QualType();
        bool isSigned = macro.consume_front("INT");
        if (!isSigned && !macro.consume_front("UINT")) return QualType();
        unsigned width = 0;
        if (macro.getAsInteger(10, width)) return QualType();
        // The target's own choice, which its <stdint.h> macros are defined from: long long for 64 bits on macOS.
        TargetInfo::IntType choice = width == 16   ? (isSigned ? target.getInt16Type() : target.getUInt16Type())
                                     : width == 64 ? (isSigned ? target.getInt64Type() : target.getUInt64Type())
                                                   : target.getIntTypeByWidth(width, isSigned);
        switch (choice) {
        case TargetInfo::SignedChar:
            return ast.SignedCharTy;
        case TargetInfo::UnsignedChar:
            return ast.UnsignedCharTy;
        case TargetInfo::SignedShort:
            return ast.ShortTy;
        case TargetInfo::UnsignedShort:
            return ast.UnsignedShortTy;
        case TargetInfo::SignedInt:
            return ast.IntTy;
        case TargetInfo::UnsignedInt:
            return ast.UnsignedIntTy;
        case TargetInfo::SignedLong:
            return ast.LongTy;
        case TargetInfo::UnsignedLong:
            return ast.UnsignedLongTy;
        case TargetInfo::SignedLongLong:
            return ast.LongLongTy;
        case TargetInfo::UnsignedLongLong:
            return ast.UnsignedLongLongTy;
        default:
            return QualType();
        }
    }

    // One token out for the one in (maybe with some read ahead and dropped): the token is changed where it is, with no
    // stream to push back. A sized type name becomes the parser's own type annotation, so no macro need expand.
    bool replaceInPlace(Token &tok, const Token &made) {
        if (!made.is(tok::identifier) || made.isExpandDisabled()) { // a keyword, or a name that needs no expansion
            tok = made;
            return true;
        }
        if (sema == nullptr) return false;
        QualType type = sizedType(made.getIdentifierInfo()->getName());
        if (type.isNull()) return false; // a macro of the program's (c::X): expand it from a stream
        Token annotation = made;
        annotation.setKind(tok::annot_typename);
        annotation.setAnnotationEndLoc(made.getLocation());
        annotation.setAnnotationValue(ParsedType::make(type).getAsOpaquePtr());
        tok = annotation;
        return true;
    }

    // Puts the output back, last piece first, since each is pushed on top of the lexer stack.
    void flush() {
        closeTokens();
        for (size_t i = actions.size(); i > 0; i--) {
            Action &action = actions[i - 1];
            if (action.include) {
                enterHeader(action);
            } else if (!action.tokens.empty()) {
                auto tokens = std::make_unique<Token[]>(action.tokens.size());
                std::copy(action.tokens.begin(), action.tokens.end(), tokens.get());
                PP.EnterTokenStream(std::move(tokens), action.tokens.size(), /*DisableMacroExpansion=*/false,
                                    /*IsReinject=*/true);
            }
        }
    }

    void closeTokens() {
        if (out.empty()) return;
        Action action;
        action.tokens = std::move(out);
        actions.push_back(std::move(action));
        out.clear();
    }

    void enterHeader(const Action &action) {
        std::string text = std::string("#include ") + (action.angled ? "<" : "\"") + action.header +
                           (action.angled ? ">" : "\"") + "\n";
        auto buffer = llvm::MemoryBuffer::getMemBufferCopy(text, "<smith-import>");
        FileID id = SM.createFileID(std::move(buffer), SrcMgr::C_User, 0, 0, action.at);
        importFiles.insert(id.getHashValue());
        PP.EnterSourceFile(id, nullptr, action.at);
    }

    static void nextToken(void *context, SmithToken *token, bool unexpanded) {
        auto *self = static_cast<SmithHook *>(context);
        Token next;
        if (unexpanded) {
            self->PP.LexUnexpandedToken(next);
        } else {
            self->PP.Lex(next);
        }
        self->pool.push_back(next);
        *token = self->convert(next, self->pool.size() - 1);
    }

    Token makeToken(const SmithToken *token) {
        Token made;
        made.startToken();
        size_t length = token->length;
        char *text = texts.Allocate<char>(length + 1);
        std::memcpy(text, token->text, length);
        text[length] = 0;
        if (token->kind == SmithTokenKind_Identifier) {
            made.setKind(tok::raw_identifier);
            made.setRawIdentifierData(text);
            made.setLength(length);
            PP.LookUpIdentifierInfo(made);
        } else {
            Lexer raw(SourceLocation(), PP.getLangOpts(), text, text, text + length);
            raw.LexFromRawLexer(made);
        }
        return made;
    }

    static void emitToken(void *context, const SmithToken *token) {
        auto *self = static_cast<SmithHook *>(context);
        Token made;
        if (token->handle != smith_token_no_handle) {
            made = self->pool[token->handle];
        } else {
            made = self->makeToken(token);
        }
        made.setLocation(SourceLocation::getFromRawEncoding(token->location));
        made.clearFlag(Token::StartOfLine);
        made.clearFlag(Token::LeadingSpace);
        made.clearFlag(Token::DisableExpand);
        if (token->line_start) made.setFlag(Token::StartOfLine);
        if (token->space_before) made.setFlag(Token::LeadingSpace);
        if (!token->expand && made.getIdentifierInfo() != nullptr) made.setFlag(Token::DisableExpand);
        self->out.push_back(made);
    }

    static void includeHeader(void *context, const char *header, bool angled) {
        auto *self = static_cast<SmithHook *>(context);
        self->closeTokens();
        Action action;
        action.include = true;
        action.angled = angled;
        action.header = header;
        if (!angled) { // a quoted header is found beside the file that imports it, as #include "h" would
            llvm::SmallString<256> beside(llvm::sys::path::parent_path(self->mainName));
            llvm::sys::path::append(beside, header);
            if (llvm::sys::fs::exists(beside)) action.header = std::string(beside);
        }
        action.at = self->pool.empty() ? SourceLocation() : self->pool.front().getLocation();
        self->actions.push_back(std::move(action));
    }

    static void reportError(void *context, uint32_t location, const char *message) {
        auto *self = static_cast<SmithHook *>(context);
        self->PP.getDiagnostics().Report(SourceLocation::getFromRawEncoding(location), self->diagnostic)
            << StringRef(message);
    }

    // The declaration a name means at this point of the parse, or null.
    NamedDecl *findDecl(const char *name, uint32_t length, SmithLookupKind space) {
        if (sema == nullptr || sema->getCurScope() == nullptr) return nullptr;
        IdentifierInfo *id = PP.getIdentifierInfo(StringRef(name, length));
        if (id->getFETokenInfo() == nullptr) return nullptr; // nothing is declared with that name
        LookupResult found(*sema, id, SourceLocation(),
                           space == SmithLookup_Tag ? Sema::LookupTagName : Sema::LookupOrdinaryName);
        if (!sema->LookupName(found, sema->getCurScope()) || found.empty()) return nullptr;
        return found.getRepresentativeDecl();
    }

    // The complete type a typedef or tag names, or a null type.
    QualType completeType(const char *name, uint32_t length, SmithLookupKind space) {
        auto *decl = dyn_cast_or_null<TypeDecl>(findDecl(name, length, space));
        if (decl == nullptr) return QualType();
        QualType type = sema->getASTContext().getTypeDeclType(decl);
        if (type.isNull() || type->isIncompleteType() || type->isDependentType()) return QualType();
        return type;
    }

    static bool typeInfo(void *context, const char *name, uint32_t length, SmithLookupKind space, SmithTypeInfo *info) {
        auto *self = static_cast<SmithHook *>(context);
        QualType type = self->completeType(name, length, space);
        if (type.isNull()) return false;
        ASTContext &ast = self->sema->getASTContext();
        info->size = ast.getTypeSizeInChars(type).getQuantity();
        info->alignment = ast.getTypeAlignInChars(type).getQuantity();
        info->fields = 0;
        info->is_record = false;
        if (const RecordDecl *record = type->getAsRecordDecl()) {
            info->is_record = true;
            info->fields = std::distance(record->field_begin(), record->field_end());
        }
        return true;
    }

    static bool fieldOf(void *context, const char *name, uint32_t length, SmithLookupKind space, uint32_t index,
                        const char **field_name, const char **field_type) {
        auto *self = static_cast<SmithHook *>(context);
        QualType type = self->completeType(name, length, space);
        const RecordDecl *record = type.isNull() ? nullptr : type->getAsRecordDecl();
        if (record == nullptr) return false;
        uint32_t at = 0;
        for (const FieldDecl *field : record->fields()) {
            if (at++ != index) continue;
            self->fieldName = field->getName().str();
            self->fieldType = field->getType().getAsString(self->sema->getASTContext().getPrintingPolicy());
            *field_name = self->fieldName.c_str();
            *field_type = self->fieldType.c_str();
            return true;
        }
        return false;
    }

    static void watchName(void *context, const char *name, uint32_t length) {
        auto *self = static_cast<SmithHook *>(context);
        self->PP.getIdentifierInfo(StringRef(name, length))->setHookWatched(true);
    }

    static void watchMode(void *context, SmithWatch mode) {
        static_cast<SmithHook *>(context)->PP.setHookMode(mode == SmithWatch_All      ? Preprocessor::HookAll
                                                          : mode == SmithWatch_Braces ? Preprocessor::HookBraces
                                                                                      : Preprocessor::HookWatched);
    }

    // The token `back` places before the one offered, from the Preprocessor's record of the parser's tokens.
    static bool previousToken(void *context, unsigned back, SmithToken *token) {
        auto *self = static_cast<SmithHook *>(context);
        const IdentifierInfo *info = nullptr;
        tok::TokenKind kind = self->PP.getHookPrevious(back, info);
        if (kind == tok::unknown) return false;
        *token = SmithToken{};
        token->handle = smith_token_no_handle;
        token->kind = SmithTokenKind_Punctuator;
        token->text = "";
        if (info != nullptr) {
            token->kind = SmithTokenKind_Identifier;
            token->text = info->getNameStart();
            token->length = info->getLength();
        } else if (const char *spelling = tok::getPunctuatorSpelling(kind)) {
            token->text = spelling;
            token->length = spelling[1] == 0 ? 1 : spelling[2] == 0 ? 2 : 3;
        } else if (const char *keyword = tok::getKeywordSpelling(kind)) {
            token->kind = SmithTokenKind_Identifier;
            token->text = keyword;
            token->length = std::strlen(keyword);
        }
        return true;
    }

    // Whether anything in the file has been declared: an import comes before the first declaration.
    static bool declaredInFile(void *context) {
        auto *self = static_cast<SmithHook *>(context);
        if (self->sema == nullptr) return false;
        for (const Decl *decl : self->sema->getASTContext().getTranslationUnitDecl()->decls()) {
            if (!decl->isImplicit() && self->SM.getFileID(self->SM.getFileLoc(decl->getLocation())) == self->MainFile) {
                return true;
            }
        }
        return false;
    }

    static SmithLookup lookupName(void *context, const char *name, uint32_t length, SmithLookupKind space) {
        auto *self = static_cast<SmithHook *>(context);
        SmithLookup answer{};
        NamedDecl *decl = self->findDecl(name, length, space);
        if (decl == nullptr) return answer;
        answer.kind = space == SmithLookup_Tag ? SmithLookup_Tag
                      : isa<TypeDecl>(decl)    ? SmithLookup_Type
                                               : SmithLookup_Value;
        FileID where = self->SM.getFileID(self->SM.getFileLoc(decl->getLocation()));
        answer.in_main_file = where == self->MainFile;
        answer.from_import = self->importOwned(where);
        return answer;
    }

    static bool readHeader(void *context, const char *relative, const char **text, uint32_t *length) {
        auto *self = static_cast<SmithHook *>(context);
        for (DirectoryLookup &directory : self->PP.getHeaderSearchInfo().search_dir_range()) {
            if (!directory.isNormalDir()) continue;
            std::string path = directory.getName().str() + "/" + relative;
            auto buffer = llvm::MemoryBuffer::getFile(path);
            if (!buffer) continue;
            self->headerFile = std::move(*buffer);
            *text = self->headerFile->getBufferStart();
            *length = self->headerFile->getBufferSize();
            return true;
        }
        return false;
    }
};

std::unique_ptr<TokenHook> makeHook(Preprocessor &pp) {
    SourceManager &sm = pp.getSourceManager();
    OptionalFileEntryRef entry = sm.getFileEntryRefForID(sm.getMainFileID());
    if (!entry || !entry->getName().ends_with(".smith")) return nullptr;
    return std::make_unique<SmithHook>(pp);
}

} // namespace

// Makes every translation unit in this process offer its .smith main file to the lowering.
extern "C" void smith_hook_install() {
    Preprocessor::TokenHookFactory = makeHook;
}
