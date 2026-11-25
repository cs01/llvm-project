set(LLVM_LINK_COMPONENTS
  Core
  FrontendOffloading
  ipo
  Linker
  Support
  )

add_clang_library(clangCIRFrontendAction
  CIRGenAction.cpp
  CIRDiagnosticHandler.cpp
  SafeCOwnership.cpp

  DEPENDS
  mlir-headers

  LINK_LIBS
  clangAST
  clangAnalysisLifetimeSafety
  clangSema
  MLIRCIRTransforms
  MLIRPass
  clangBasic
  clangFrontend
  clangCIR
  clangCIRLoweringCommon
  clangCIRLoweringDirectToLLVM
  clangCodeGen
  clangCodeGenUtils
  CIRRegisterAllDialects
  MLIRCIR
  MLIRBytecodeWriter
  MLIRIR
  MLIRParser
  )
