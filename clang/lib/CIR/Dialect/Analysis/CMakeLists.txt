add_clang_library(MLIRCIRAnalysis
  CIRAliasAnalysis.cpp
  CIRBasicAliasAnalysis.cpp
  CIRNullabilitySafety.cpp
  CIRPointeeWrites.cpp
  CIRReturnOrigins.cpp

  DEPENDS
  MLIRPtrMemorySpaceInterfacesIncGen

  LINK_LIBS PUBLIC
  MLIRCIR
  MLIRIR
  MLIRAnalysisBase
  MLIRSideEffectInterfaces
)
