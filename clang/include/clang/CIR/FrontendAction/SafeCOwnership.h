//===- SafeCOwnership.h - ownership rules of Safe C, checked on CIR -------===//
#ifndef CLANG_CIR_FRONTENDACTION_SAFECOWNERSHIP_H
#define CLANG_CIR_FRONTENDACTION_SAFECOWNERSHIP_H
#include <memory>
namespace clang {
class CompilerInstance;
class FrontendAction;
} // namespace clang
namespace cir {
/// Run \p Inner, after checking the same file's ownership rules on CIR that is
/// built for the check and thrown away.
std::unique_ptr<clang::FrontendAction>
wrapWithSafeCOwnership(std::unique_ptr<clang::FrontendAction> Inner);
} // namespace cir
#endif
