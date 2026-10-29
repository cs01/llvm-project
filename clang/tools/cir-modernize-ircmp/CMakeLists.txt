set(LLVM_LINK_COMPONENTS
  Core
  IRReader
  Support
  )

add_clang_tool(cir-modernize-ircmp
  cir-modernize-ircmp.cpp
  )

clang_target_link_libraries(cir-modernize-ircmp
  PRIVATE
  clangCIRModernizeIRCompare
  )
