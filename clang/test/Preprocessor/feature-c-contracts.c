// RUN: %clang_cc1 -fc-contracts -E %s | FileCheck -check-prefix=ON %s
// RUN: %clang_cc1 -E %s | FileCheck -check-prefix=OFF %s

#if __has_feature(c_contracts)
int has_contracts(void);
#else
int no_contracts(void);
#endif

// ON: int has_contracts(void);
// OFF: int no_contracts(void);
