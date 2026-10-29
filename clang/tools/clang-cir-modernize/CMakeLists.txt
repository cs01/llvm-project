set(LLVM_LINK_COMPONENTS
  BitReader
  BitWriter
  Core
  Support
  )

include_directories(${LLVM_MAIN_SRC_DIR}/../mlir/include)
include_directories(${LLVM_BINARY_DIR}/tools/mlir/include)

add_clang_tool(clang-cir-modernize
  ClangCIRModernize.cpp
  )

clang_target_link_libraries(clang-cir-modernize
  PRIVATE
  clangAST
  clangBasic
  clangFrontend
  clangLex
  clangSerialization
  clangTooling
  clangToolingCore
  clangCIR
  clangCIRModernize
  clangCIRModernizeIRCompare
  clangCodeGen
  )

target_link_libraries(clang-cir-modernize
  PRIVATE
  CIRRegisterAllDialects
  MLIRCIR
  MLIRIR
  )
