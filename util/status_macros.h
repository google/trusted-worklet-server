/*
 * Copyright 2021 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef UTIL_STATUS_MACROS_H_
#define UTIL_STATUS_MACROS_H_

#include "absl/base/optimization.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace aviary {
namespace internal {

// This template is chosen if the input object has a function called "status".
template <typename T>
inline constexpr bool HasStatus(decltype(std::declval<T>().status())*) {
  return true;
}

// Default template, chosen when no prior templates match.
template <typename T>
inline constexpr bool HasStatus(...) {
  return false;
}

// `StatusOr`-like overload which returns a wrapped `Status`-like value.
template <typename T,
          typename std::enable_if<HasStatus<T>(nullptr), int>::type = 0>
inline auto ToStatus(T&& status_or) -> decltype(status_or.status()) {
  return status_or.status();
}

// Identity function for all `Status`-like objects.
template <typename T,
          typename std::enable_if<!HasStatus<T>(nullptr), int>::type = 0>
inline T ToStatus(T&& status_like) {
  return status_like;
}

}  // namespace internal
}  // namespace aviary

/// Evaluates an expression that produces an `Status`-like object with
/// a `.ok()` method. If this method returns false, the object is
/// returned from the current function. If the expression evaluates to a
/// `StatusOr` object, then it is converted to a `Status` on return.
///
/// Example:
/// ```
///   ::absl::Status MultiStepFunction() {
///     RETURN_IF_ERROR(Function(args...));
///     RETURN_IF_ERROR(foo.Method(args...));
///     return ::absl::Status::OkStatus();
///   }
/// ```
#define RETURN_IF_ERROR(expr)                                 \
  do {                                                        \
    auto _aviary_status_to_verify = (expr);                   \
    if (ABSL_PREDICT_FALSE(!_aviary_status_to_verify.ok())) { \
      return ::aviary::internal::ToStatus(                    \
          std::forward<decltype(_aviary_status_to_verify)>(   \
              _aviary_status_to_verify));                     \
    }                                                         \
  } while (false)

/// Evaluates an expression `rexpr` that returns a `StatusOr`-like
/// object with `.ok()`, `.status()`, and `.value()` methods.  If
/// the result is OK, moves its value into the variable defined by
/// `lhs`, otherwise returns the result of the `.status()` from the
/// current function. The error result of `.status` is returned
/// unchanged. If there is an error, `lhs` is not evaluated: thus any
/// side effects of evaluating `lhs` will only occur if `rexpr.ok()`
/// is true.
///
/// Interface:
/// ```
///   ASSIGN_OR_RETURN(lhs, rexpr)
/// ```
///
/// Example: Assigning to an existing variable:
/// ```
///   ValueType value;
///   ASSIGN_OR_RETURN(value, MaybeGetValue(arg));
/// ```
///
/// Example: Assigning to an expression with side effects:
/// ```
///   MyProto data;
///   ASSIGN_OR_RETURN(*data.mutable_str(), MaybeGetValue(arg));
///   // No field "str" is added on error.
/// ```
///
/// Example: Assigning to a `std::unique_ptr`.
/// ```
///   std::unique_ptr<T> ptr;
///   ASSIGN_OR_RETURN(ptr, MaybeGetPtr(arg));
/// ```
///
/// Example: Defining and assigning a value to a new variable:
/// ```
///   ASSIGN_OR_RETURN(ValueType value, MaybeGetValue(arg));
/// ```
#define ASSIGN_OR_RETURN(lhs, rexpr)                                     \
  AVIARY_ASSIGN_OR_RETURN_IMPL_(                                         \
      AVIARY_STATUS_MACROS_IMPL_CONCAT_(status_or_value, __LINE__), lhs, \
      rexpr)

// Internal helper.
#define AVIARY_ASSIGN_OR_RETURN_IMPL_(statusor, lhs, rexpr) \
  auto statusor = (rexpr);                                  \
  if (ABSL_PREDICT_FALSE(!statusor.ok())) {                 \
    return std::move(statusor).status();                    \
  }                                                         \
  lhs = std::move(statusor).value()

// Internal helper for concatenating macro values.
#define AVIARY_STATUS_MACROS_IMPL_CONCAT_INNER_(x, y) x##y
#define AVIARY_STATUS_MACROS_IMPL_CONCAT_(x, y) \
  AVIARY_STATUS_MACROS_IMPL_CONCAT_INNER_(x, y)

#endif  // UTIL_STATUS_MACROS_H_
