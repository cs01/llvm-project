set(LLVM_LINK_COMPONENTS
  Core
  Demangle
  Passes
  Support
  )

add_clang_library(clangCIRModernizeIRCompare
  IRCompare.cpp
  )
