// SPIKE of #189 (throwaway): a plugin linked into smith that runs on every compile with no flag of the user's.
//   - AffineAttr: `[[smith::affine]]` is a known attribute (a ParsedAttrInfo in the static registry, so the compiler,
//     clang-tidy and clangd all know it, no -load, no flag), kept in the AST as AnnotateAttr("smith::affine").
//   - AffineProbe: a PluginASTAction "smith-affine" (the driver is given `-add-plugin smith-affine`, shim/clang.cpp),
//     which reports an error for a function named affine_probe and for an affine type's declaration, builds the CFG of
//     every function (cost and availability), and finds a second use of a variable of an affine type in source order.
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Analysis/CFG.h"
#include "clang/Basic/ParsedAttrInfo.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Sema/ParsedAttr.h"
#include "clang/Sema/Sema.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdlib>
#include <map>

using namespace clang;

static bool enabled = true;

namespace {

bool isAffine(QualType type) {
    const Decl *decl = nullptr;
    if (const auto *typedef_type = type->getAs<TypedefType>()) {
        decl = typedef_type->getDecl();
        if (decl->hasAttr<AnnotateAttr>())
            for (const auto *a : decl->specific_attrs<AnnotateAttr>())
                if (a->getAnnotation() == "smith::affine") return true;
    }
    if (const TagDecl *tag = type->getAsTagDecl())
        for (const auto *a : tag->specific_attrs<AnnotateAttr>())
            if (a->getAnnotation() == "smith::affine") return true;
    return false;
}

struct AffineAttr final : ParsedAttrInfo {
    AffineAttr() {
        static constexpr Spelling spellings[] = {{ParsedAttr::AS_C23, "smith::affine"},
                                                 {ParsedAttr::AS_CXX11, "smith::affine"},
                                                 {ParsedAttr::AS_GNU, "smith_affine"}};
        Spellings = spellings;
    }
    bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr, const Decl *D) const override {
        if (isa<TypedefNameDecl>(D) || isa<TagDecl>(D)) return true;
        S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type)
            << Attr << Attr.isRegularKeywordAttribute() << ExpectedTypeOrNamespace;
        return false;
    }
    AttrHandling handleDeclAttribute(Sema &S, Decl *D, const ParsedAttr &Attr) const override {
        D->addAttr(AnnotateAttr::Create(S.Context, "smith::affine", nullptr, 0, Attr.getRange()));
        return AttributeApplied;
    }
};

class Visitor : public RecursiveASTVisitor<Visitor> {
  public:
    Visitor(CompilerInstance &ci, unsigned error, unsigned note) : ci(ci), error(error), note(note) {}
    bool VisitFunctionDecl(FunctionDecl *f) {
        if (f->getIdentifier() != nullptr && f->getName() == "affine_probe") ci.getDiagnostics().Report(f->getLocation(), error) << "function affine_probe";
        const SourceManager &sm = f->getASTContext().getSourceManager();
        // CFG::buildCFG crashes on a dependent body (a template): skip them, and what a header defines.
        if (f->hasBody() && !f->isDependentContext() && !f->isInvalidDecl() && sm.isInMainFile(f->getLocation())) {
            used.clear();
            std::unique_ptr<CFG> cfg = CFG::buildCFG(f, f->getBody(), &f->getASTContext(), CFG::BuildOptions());
            if (cfg) {
                blocks += cfg->size();
                functions += 1;
            }
        }
        return true;
    }
    bool VisitTypedefNameDecl(TypedefNameDecl *t) {
        if (isAffine(t->getUnderlyingType()) || t->hasAttr<AnnotateAttr>())
            ci.getDiagnostics().Report(t->getLocation(), error) << "affine type " + t->getName().str();
        return true;
    }
    bool VisitDeclRefExpr(DeclRefExpr *r) {
        const auto *v = dyn_cast<VarDecl>(r->getDecl());
        if (v == nullptr || !isAffine(v->getType())) return true;
        auto at = used.find(v);
        if (at == used.end()) {
            used[v] = r->getBeginLoc();
            return true;
        }
        ci.getDiagnostics().Report(r->getBeginLoc(), error) << "second use of " + v->getName().str();
        ci.getDiagnostics().Report(at->second, note) << "first use";
        return true;
    }
    unsigned blocks = 0, functions = 0;

  private:
    CompilerInstance &ci;
    unsigned error, note;
    std::map<const VarDecl *, SourceLocation> used;
};

class Consumer : public ASTConsumer {
  public:
    explicit Consumer(CompilerInstance &ci) : ci(ci) {}
    void HandleTranslationUnit(ASTContext &context) override {
        DiagnosticsEngine &d = ci.getDiagnostics();
        unsigned error = d.getCustomDiagID(DiagnosticsEngine::Error, "smith-affine: %0");
        unsigned note = d.getCustomDiagID(DiagnosticsEngine::Note, "smith-affine: %0");
        Visitor visitor(ci, error, note);
        visitor.TraverseDecl(context.getTranslationUnitDecl());
        if (std::getenv("SMITH_AFFINE_STATS"))
            llvm::errs() << "smith-affine: " << visitor.functions << " functions, " << visitor.blocks << " blocks\n";
    }

  private:
    CompilerInstance &ci;
};

struct AffineProbe final : PluginASTAction {
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &ci, StringRef) override {
        if (!enabled) return std::make_unique<ASTConsumer>(); // never null: the multiplexer calls it
        return std::make_unique<Consumer>(ci);
    }
    bool ParseArgs(const CompilerInstance &, const std::vector<std::string> &) override { return true; }
    ActionType getActionType() override { return AddAfterMainAction; }
};

const ParsedAttrInfoRegistry::Add<AffineAttr> attribute("smith-affine-attr", "[[smith::affine]]");
const FrontendPluginRegistry::Add<AffineProbe> plugin("smith-affine", "a value of an affine type is used once");

} // namespace

// clang-tidy's own consumer of diagnostics cannot name a custom id (it crashes in getCheckName), so `smith lint` runs
// the analysis as a check instead and turns the plugin off.
extern "C" void smith_affine_disable() {
    enabled = false;
}
