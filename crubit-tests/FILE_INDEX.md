# Crubit Nullability Checker Test Files

Source: https://github.com/google/crubit
Path: nullability/test/

## Complete file listing from the test directory

### Source Files (.cc):
- arrays.cc
- basic.cc
- binary_ops.cc
- casts.cc
- check_diagnostics.cc
- check_diagnostics_test.cc
- comparisons.cc
- consistent_annotations.cc
- constructors.cc
- convergence.cc
- default_arguments.cc
- default_member_initializers.cc
- default_member_initializers_smart_pointers.cc
- fields.cc
- forwarding_functions.cc
- forwarding_functions_diagnosis.cc
- function_calls.cc
- function_calls_diagnosis.cc
- function_pointers.cc
- initialization.cc
- join.cc
- join_diagnosis.cc
- nested.cc
- operator_new.cc
- parens.cc
- path_sensitive.cc
- pointer_arithmetic.cc
- pointer_arithmetic_diagnosis.cc
- pragma.cc
- pragma_diagnosis.cc
- resugaring.cc
- return_statements.cc
- smart_pointers.cc
- smart_pointers_diagnosis.cc
- struct_initialization.cc
- symbolic_nullability.cc
- templates.cc
- temporary_materialization.cc
- this_pointer.cc
- type_aliases.cc
- types.cc
- variable_aliasing.cc
- variance.cc

### Header Files (.h):
- check.h
- check_diagnostics.h
- check_diagnostics_preamble.h
- pragma_none.h
- pragma_nonnull.h
- pragma_support.h
- nullability_annotations.h

### Other Files:
- nullability_test.bzl
- nullability_test.sh
- nullability_test_test.sh
- memory
- new
- optional
- type_traits
- utility
- BUILD

## Test Framework

Two test styles are used:

1. **checkDiagnostics style** (gtest): Used by basic.cc, return_statements.cc,
   function_calls_diagnosis.cc, etc. Tests use `EXPECT_TRUE(checkDiagnostics(R"cc(...)cc"))`.
   Diagnostics expected on lines marked with `// [[unsafe]]`.

2. **nullability_test style** (custom): Used by comparisons.cc, binary_ops.cc,
   pointer_arithmetic.cc, etc. Tests use `TEST void funcname(...)` and helper
   functions like `nonnull()`, `nullable()`, `unknown()`, `type<>()`, `provable()`.

## Files NOT fetched (can be retrieved later):
- check_diagnostics.cc (implementation)
- consistent_annotations.cc
- default_arguments.cc
- default_member_initializers.cc
- default_member_initializers_smart_pointers.cc
- forwarding_functions.cc
- forwarding_functions_diagnosis.cc
- function_pointers.cc
- operator_new.cc
- parens.cc
- pragma.cc
- pragma_diagnosis.cc
- resugaring.cc
- smart_pointers.cc
- struct_initialization.cc
- symbolic_nullability.cc
- temporary_materialization.cc
- this_pointer.cc
- type_aliases.cc
- types.cc
- variance.cc
