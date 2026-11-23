#include "clang/Sema/SemaBase.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaCUDA.h"

namespace clang {

SemaBase::SemaBase(Sema &S) : SemaRef(S) {}

ASTContext &SemaBase::getASTContext() const { return SemaRef.Context; }
DiagnosticsEngine &SemaBase::getDiagnostics() const { return SemaRef.Diags; }
const LangOptions &SemaBase::getLangOpts() const { return SemaRef.LangOpts; }
DeclContext *SemaBase::getCurContext() const { return SemaRef.CurContext; }

SemaBase::ImmediateDiagBuilder::~ImmediateDiagBuilder() {
  // If we aren't active, there is nothing to do.
  if (!isActive())
    return;

  // Otherwise, we need to emit the diagnostic. First clear the diagnostic
  // builder itself so it won't emit the diagnostic in its own destructor.
  //
  // This seems wasteful, in that as written the DiagnosticBuilder dtor will
  // do its own needless checks to see if the diagnostic needs to be
  // emitted. However, because we take care to ensure that the builder
  // objects never escape, a sufficiently smart compiler will be able to
  // eliminate that code.
  Clear();

  // Dispatch to Sema to emit the diagnostic.
  SemaRef.EmitDiagnostic(DiagID, *this);
}

PartialDiagnostic SemaBase::PDiag(unsigned DiagID) {
  return PartialDiagnostic(DiagID, SemaRef.Context.getDiagAllocator());
}

const SemaBase::SemaDiagnosticBuilder &
operator<<(const SemaBase::SemaDiagnosticBuilder &Diag,
           const PartialDiagnostic &PD) {
  if (Diag.ImmediateDiag)
    PD.Emit(*Diag.ImmediateDiag);
  else if (Diag.PartialDiagId)
    Diag.S.DeviceDeferredDiags[Diag.Fn][*Diag.PartialDiagId].second = PD;
  return Diag;
}

void SemaBase::SemaDiagnosticBuilder::AddFixItHint(
    const FixItHint &Hint) const {
  if (ImmediateDiag)
    ImmediateDiag->AddFixItHint(Hint);
  else if (PartialDiagId)
    S.DeviceDeferredDiags[Fn][*PartialDiagId].second.AddFixItHint(Hint);
}

llvm::DenseMap<CanonicalDeclPtr<const FunctionDecl>,
               std::vector<PartialDiagnosticAt>> &
SemaBase::SemaDiagnosticBuilder::getDeviceDeferredDiags() const {
  return S.DeviceDeferredDiags;
}

/// Under -fsafe-c, a safety warning is re-issued as an error with the same
/// text. The error is a custom diagnostic with no warning group, so no -Wno-*,
/// -Wno-error=* or diagnostic pragma can lower it. Safety warnings are the
/// nullability-safety groups, and the bounds-safety warnings that default to
/// error (Apple's "this is an error, but you may downgrade it" class).
/// System headers keep the original warning: they are not Safe C code.
static unsigned safeCErrorFor(DiagnosticsEngine &Diags, SourceManager &SM,
                              SourceLocation Loc, unsigned DiagID) {
  DiagnosticIDs &IDs = *Diags.getDiagnosticIDs();
  if (!IDs.isWarningOrExtension(DiagID))
    return DiagID;
  StringRef Group = IDs.getWarningOptionForDiag(DiagID);
  bool IsSafety = Group.starts_with("nullability-safety") ||
                  (Group.starts_with("bounds-safety") &&
                   IDs.isDefaultMappingAsError(DiagID));
  if (!IsSafety || (Loc.isValid() && SM.isInSystemHeader(Loc)))
    return DiagID;
  return IDs.getCustomDiagID(DiagnosticIDs::CustomDiagDesc(
      diag::Severity::Error, std::string(IDs.getDescription(DiagID)),
      DiagnosticIDs::CLASS_ERROR));
}

Sema::SemaDiagnosticBuilder SemaBase::Diag(SourceLocation Loc,
                                           unsigned DiagID) {
  if (getLangOpts().SafeC) {
    unsigned NewID = safeCErrorFor(getDiagnostics(),
                                   SemaRef.getSourceManager(), Loc, DiagID);
    if (NewID != DiagID) {
      ++SemaRef.NumSafeCSafetyErrors;
      DiagID = NewID;
    }
  }
  bool IsError =
      getDiagnostics().getDiagnosticIDs()->isDefaultMappingAsError(DiagID);
  bool ShouldDefer = getLangOpts().CUDA && getLangOpts().GPUDeferDiag &&
                     DiagnosticIDs::isDeferrable(DiagID) &&
                     (SemaRef.DeferDiags || !IsError);
  // Even without -fgpu-defer-diag, defer device-side errors inside an
  // implicit-H+D explicit instantiation so end-of-TU classification can
  // choose between surfacing them or emitting a trap body.
  if (!ShouldDefer && getLangOpts().CUDA && getLangOpts().CUDAIsDevice &&
      DiagnosticIDs::isDeferrable(DiagID) &&
      SemaCUDA::isImplicitHDExplicitInstantiation(
          SemaRef.getCurFunctionDecl(/*AllowLambda=*/true)))
    ShouldDefer = true;
  auto SetIsLastErrorImmediate = [&](bool Flag) {
    if (IsError)
      SemaRef.IsLastErrorImmediate = Flag;
  };
  if (!ShouldDefer) {
    SetIsLastErrorImmediate(true);
    return SemaDiagnosticBuilder(SemaDiagnosticBuilder::K_Immediate, Loc,
                                 DiagID, SemaRef.getCurFunctionDecl(), SemaRef);
  }

  SemaDiagnosticBuilder DB = getLangOpts().CUDAIsDevice
                                 ? SemaRef.CUDA().DiagIfDeviceCode(Loc, DiagID)
                                 : SemaRef.CUDA().DiagIfHostCode(Loc, DiagID);
  SetIsLastErrorImmediate(DB.isImmediate());
  return DB;
}

Sema::SemaDiagnosticBuilder SemaBase::Diag(SourceLocation Loc,
                                           const PartialDiagnostic &PD) {
  return Diag(Loc, PD.getDiagID()) << PD;
}

SemaBase::SemaDiagnosticBuilder SemaBase::DiagCompat(SourceLocation Loc,
                                                     unsigned CompatDiagId) {
  return Diag(Loc, DiagnosticIDs::getCompatDiagId(getLangOpts(), CompatDiagId));
}
} // namespace clang
