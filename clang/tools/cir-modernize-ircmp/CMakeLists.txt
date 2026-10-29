set(LLVM_LINK_COMPONENTS
  Core
  Demangle
  IRReader
  Support
  )

add_clang_tool(cir-modernize-ircmp
  cir-modernize-ircmp.cpp
  )
