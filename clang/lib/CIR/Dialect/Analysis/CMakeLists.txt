add_clang_library(MLIRCIRAnalysis
  CIRAliasAnalysis.cpp
  CIRBasicAliasAnalysis.cpp
  CIRNullabilitySafety.cpp
  CIRPointeeWrites.cpp

  DEPENDS
  MLIRPtrMemorySpaceInterfacesIncGen

  LINK_LIBS PUBLIC
  MLIRCIR
  MLIRIR
  MLIRAnalysisBase
  MLIRSideEffectInterfaces
)
