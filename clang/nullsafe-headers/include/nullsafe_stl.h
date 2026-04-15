/*
 * nullsafe_stl.h - STL nullability contract documentation
 *
 * C++ template member functions cannot be re-declared with _Nonnull/_Nullable
 * in overlay headers the way C functions can. Instead, the nullsafe flow
 * analysis has a built-in allowlist (in FlowNullability.cpp, function
 * isStlNonnullReturnCall) that recognizes specific std:: methods as returning
 * nonnull pointers.
 *
 * This header documents those contracts. Include it to get the annotated
 * free-function helpers below, but the core STL method recognition happens
 * inside the compiler regardless of whether this header is included.
 *
 * Recognized nonnull-returning STL methods:
 *
 *   std::vector<T>::data()           - always valid (points into buffer)
 *   std::vector<T>::begin()          - always valid iterator
 *   std::vector<T>::end()            - always valid past-the-end iterator
 *
 *   std::basic_string<T>::c_str()    - always valid (null-terminated)
 *   std::basic_string<T>::data()     - always valid
 *   std::basic_string<T>::begin()    - always valid
 *   std::basic_string<T>::end()      - always valid
 *
 *   std::basic_string_view<T>::begin()  - always valid
 *   std::basic_string_view<T>::end()    - always valid
 *   (Note: string_view::data() is intentionally nullable — can be nullptr)
 *
 *   std::optional<T>::operator->()   - nonnull (UB if empty)
 *
 *   std::array<T,N>::data()          - always valid
 *   std::array<T,N>::begin()         - always valid
 *   std::array<T,N>::end()           - always valid
 *
 *   std::span<T>::data()             - always valid
 *   std::span<T>::begin()            - always valid
 *   std::span<T>::end()              - always valid
 *
 * Smart pointer methods are handled separately by the smart pointer tracking
 * in FlowNullability.cpp (unique_ptr, shared_ptr, weak_ptr).
 */

#ifndef _NULLSAFE_STL_H
#define _NULLSAFE_STL_H

#ifdef __cplusplus

/*
 * Annotated free-function wrappers for common STL pointer-returning methods.
 * These are optional convenience functions — the compiler recognizes the
 * methods directly on the STL types. Use these when you want explicit
 * _Nonnull annotation visible in the source.
 */

#if __has_include(<vector>)
#include <vector>
namespace nullsafe {
template <typename T, typename Alloc>
inline T *_Nonnull data(std::vector<T, Alloc> &v) {
  return v.data();
}
template <typename T, typename Alloc>
inline const T *_Nonnull data(const std::vector<T, Alloc> &v) {
  return v.data();
}
} // namespace nullsafe
#endif

#if __has_include(<string>)
#include <string>
namespace nullsafe {
template <typename CharT, typename Traits, typename Alloc>
inline const CharT *_Nonnull c_str(
    const std::basic_string<CharT, Traits, Alloc> &s) {
  return s.c_str();
}
template <typename CharT, typename Traits, typename Alloc>
inline const CharT *_Nonnull data(
    const std::basic_string<CharT, Traits, Alloc> &s) {
  return s.data();
}
} // namespace nullsafe
#endif

#endif /* __cplusplus */

#endif /* _NULLSAFE_STL_H */
