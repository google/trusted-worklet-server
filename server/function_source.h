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

#ifndef SERVER_FUNCTION_SOURCE_H_
#define SERVER_FUNCTION_SOURCE_H_

#include "absl/status/statusor.h"
#include "absl/types/optional.h"
#include "httplib.h"

namespace aviary {
namespace server {

// Describes how a bidding function source is specified.
struct FunctionSpecification {
  // Bidding function URI. Can be a remote URI (using `http(s)` scheme) or a
  // URI using `local` scheme, in which case the bidding function source
  // code must be provided as part of the specification.
  //
  // Must be unique across all configured bidding functions.
  std::string uri;
  // Bidding function source code which must be specified if URI uses `local`
  // scheme.
  absl::optional<std::string> source_code;
};

// Retrieves function code from different sources.
class FunctionSource {
 public:
  FunctionSource() = default;
  virtual ~FunctionSource() = default;

  // Gets the function raw source code given a function specification.
  // May block in case an external resource is accessed, for example, if a
  // function specification references a remote URL.
  virtual absl::StatusOr<std::string> GetFunctionCode(
      const FunctionSpecification& specification) const;

  FunctionSource(const FunctionSource&) = delete;
  FunctionSource& operator=(const FunctionSource&) = delete;
};

}  // namespace server
}  // namespace aviary

#endif  // SERVER_FUNCTION_SOURCE_H_
