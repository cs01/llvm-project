//===-- c_contracts.h - C contracts runtime interface ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef __C_CONTRACTS_H
#define __C_CONTRACTS_H

#ifdef __cplusplus
extern "C" __attribute__((__noreturn__)) void
__contract_violation(const char *predicate, const char *file, unsigned line,
                     const char *function);
#else
__attribute__((__noreturn__)) void
__contract_violation(const char *predicate, const char *file, unsigned line,
                     const char *function);
#endif

#endif
