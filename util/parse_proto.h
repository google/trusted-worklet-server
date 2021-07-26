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

#ifndef UTIL_PARSE_PROTO_H_
#define UTIL_PARSE_PROTO_H_

#include "absl/strings/string_view.h"
#include "google/protobuf/text_format.h"

namespace aviary {
namespace util {
/// Parses a given string into a protocol buffer message of the template type
/// argument.
template <typename T>
T ParseTextOrDie(absl::string_view input) {
  T result;
  ABSL_ASSERT(google::protobuf::TextFormat::ParseFromString(std::string(input),
                                                            &result));
  return result;
}
}  // namespace util
}  // namespace aviary

#endif  // UTIL_PARSE_PROTO_H_
