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

#ifndef UTIL_UNUSED_PORT_H_H_
#define UTIL_UNUSED_PORT_H_H_

#include "absl/status/statusor.h"
namespace aviary {
namespace util {
// Finds an unused local TCP port.
absl::StatusOr<int> FindUnusedPort();
}  // namespace util
}  // namespace aviary

#endif  // UTIL_UNUSED_PORT_H_H_
