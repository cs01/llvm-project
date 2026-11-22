#define T(x) printf("%-28s recognized=%d\n", #x, __has_attribute(x));
#define TC(x) printf("%-28s recognized=%d\n", "[[clang::" #x "]]", __has_cpp_attribute(clang::x));
#include <stdio.h>
int main(void) {
  T(counted_by) T(sized_by) T(counted_by_or_null) T(ended_by) T(alloc_size) T(pass_object_size) T(access)
  T(nonnull) T(returns_nonnull) T(_Nonnull)
  T(noescape) T(lifetimebound) T(lifetime_capture_by) T(ownership_returns) T(ownership_takes) T(ownership_holds) T(malloc) T(cf_returns_retained) T(cf_consumed) T(ns_returns_retained)
  T(warn_unused_result) T(swift_error) T(swift_async_error)
  T(callback) T(format) T(sentinel) T(enum_extensibility) T(flag_enum) T(nonstring)
  return 0;
}
