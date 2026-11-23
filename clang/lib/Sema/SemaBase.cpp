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
static bool isSafeCSafetyDiag(DiagnosticIDs &IDs, unsigned DiagID) {
  if (!IDs.isWarningOrExtension(DiagID))
    return false;
  StringRef Group = IDs.getWarningOptionForDiag(DiagID);
  // Indexing a local that came from a single-element pointer traps unless the
  // index is 0: say so at compile time rather than at the trap.
  // Reading a variable that may not be initialized: Rust's definite
  // initialization rule, from clang's uninitialized-variables analysis.
  if (Group == "uninitialized" || Group == "sometimes-uninitialized" ||
      Group == "conditional-uninitialized" ||
      Group == "uninitialized-const-pointer")
    return true;
  // Falling off the end of a non-void function returns garbage the caller may
  // use as a pointer. Dropping const (a string literal is const under
  // -fsafe-c) would let code write memory that must not be written.
  if (Group == "return-type" ||
      Group == "incompatible-pointer-types-discards-qualifiers")
    return true;
  // Variadic calls are checked only through their format string, so a format
  // the compiler cannot check, or one that does not match its arguments, is
  // an error; so is a call through a declaration without a prototype.
  if (Group == "format" || Group == "format-security" ||
      Group == "format-nonliteral" || Group == "format-insufficient-args" ||
      Group == "format-invalid-specifier" ||
      Group == "deprecated-non-prototype")
    return true;
  // Lifetime safety's findings (use after free or scope, dangling pointers,
  // broken lifetimebound/noescape/capture contracts); not its suggestions.
  if (llvm::is_contained(
          {"lifetime-safety-use-after-scope",
           "lifetime-safety-use-after-scope-moved",
           "lifetime-safety-use-after-free", "lifetime-safety-return-stack-addr",
           "lifetime-safety-return-stack-addr-moved",
           "lifetime-safety-dangling-field",
           "lifetime-safety-dangling-field-moved",
           "lifetime-safety-dangling-global",
           "lifetime-safety-dangling-global-moved",
           "lifetime-safety-lifetimebound-violation", "lifetime-safety-noescape",
           "lifetime-safety-capture-by-violation"},
          Group))
    return true;
  // Safe C's own rules (safe-c-unsafe is the -R report, not a rule).
  if (Group.starts_with("safe-c-") && Group != "safe-c-unsafe")
    return true;
  return Group.starts_with("nullability-safety") ||
         Group == "safe-c-void-cast" ||
         Group.starts_with(
             "bounds-safety-conversion-single-to-implicit-indexable") ||
         (Group.starts_with("bounds-safety") &&
          IDs.isDefaultMappingAsError(DiagID));
}

bool Sema::isInSafeCUnsafeRegion(SourceLocation Loc) const {
  if (Loc.isInvalid() || SafeCUnsafeRanges.empty())
    return false;
  Loc = SourceMgr.getFileLoc(Loc);
  for (SourceRange R : SafeCUnsafeRanges) {
    SourceLocation B = SourceMgr.getFileLoc(R.getBegin());
    SourceLocation E = SourceMgr.getFileLoc(R.getEnd());
    if (!SourceMgr.isBeforeInTranslationUnit(Loc, B) &&
        !SourceMgr.isBeforeInTranslationUnit(E, Loc))
      return true;
  }
  return false;
}

void Sema::beginSafeCUnsafeRegion(SourceLocation Begin, bool IsFunction) {
  ++SafeCUnsafeDepth;
  SafeCUnsafeOpen.push_back(SafeCUnsafeRegions.size());
  SafeCUnsafeRegions.push_back({SourceRange(Begin, Begin), IsFunction});
}

void Sema::endSafeCUnsafeRegion(SourceLocation End) {
  if (SafeCUnsafeDepth == 0 || SafeCUnsafeOpen.empty())
    return;
  --SafeCUnsafeDepth;
  SafeCUnsafeRegion &R = SafeCUnsafeRegions[SafeCUnsafeOpen.pop_back_val()];
  R.Range.setEnd(End.isValid() ? End : R.Range.getBegin());
  SafeCUnsafeRanges.push_back(R.Range);
}

void Sema::noteSafeCUnsafeRegionUsed(SourceLocation Loc) {
  if (!SafeCUnsafeOpen.empty()) {
    SafeCUnsafeRegions[SafeCUnsafeOpen.back()].Used = true;
    return;
  }
  // Raised after parsing (end-of-function analyses): the innermost closed
  // region holding Loc is the latest-starting one that contains it.
  Loc = SourceMgr.getFileLoc(Loc);
  SafeCUnsafeRegion *Best = nullptr;
  for (SafeCUnsafeRegion &R : SafeCUnsafeRegions) {
    SourceLocation B = SourceMgr.getFileLoc(R.Range.getBegin());
    SourceLocation E = SourceMgr.getFileLoc(R.Range.getEnd());
    if (SourceMgr.isBeforeInTranslationUnit(Loc, B) ||
        SourceMgr.isBeforeInTranslationUnit(E, Loc))
      continue;
    if (!Best || SourceMgr.isBeforeInTranslationUnit(
                     SourceMgr.getFileLoc(Best->Range.getBegin()), B))
      Best = &R;
  }
  if (Best)
    Best->Used = true;
}

Sema::SemaDiagnosticBuilder SemaBase::Diag(SourceLocation Loc,
                                           unsigned DiagID) {
  if (getLangOpts().SafeC) {
    DiagnosticIDs &IDs = *getDiagnostics().getDiagnosticIDs();
    if (isSafeCSafetyDiag(IDs, DiagID) &&
        !(Loc.isValid() && SemaRef.getSourceManager().isInSystemHeader(Loc))) {
      // Inside __unsafe the programmer takes responsibility: issue nothing.
      // Elsewhere the error has no warning group, so nothing can lower it.
      bool InUnsafe =
          SemaRef.SafeCUnsafeDepth > 0 || SemaRef.isInSafeCUnsafeRegion(Loc);
      DiagID = IDs.getCustomDiagID(DiagnosticIDs::CustomDiagDesc(
          InUnsafe ? diag::Severity::Ignored : diag::Severity::Error,
          std::string(IDs.getDescription(DiagID)),
          InUnsafe ? DiagnosticIDs::CLASS_WARNING
                   : DiagnosticIDs::CLASS_ERROR));
      if (!InUnsafe)
        ++SemaRef.NumSafeCSafetyErrors;
      else
        SemaRef.noteSafeCUnsafeRegionUsed(Loc);
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
