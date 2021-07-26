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

#ifndef FUNCTION_BIDDING_FUNCTION_INTERFACE_H_H_
#define FUNCTION_BIDDING_FUNCTION_INTERFACE_H_H_

#include "absl/status/statusor.h"
#include "proto/bidding_function.pb.h"

namespace aviary {
namespace function {

struct FunctionOptions {
  // Whether to flatten function arguments from the fields of the input
  // object.
  //
  // When false, input message is converted to a JSON object, which is
  // provided as a single argument to the function.
  //
  // When true, each field in the input message  converted into a JSON
  // value becomes an individual argument to the function in the order of
  // declaration in the protocol buffer.
  bool flatten_function_arguments = false;
};

// JavaScript function that executes the sandboxed bidding and auction logic.
template <typename Input, typename Output>
class BiddingFunctionInterface {
 public:
  // Invokes the function for as many `bidding_function_inputs` as are provided.
  // Returns a vector of resulting bid price CPMs in the order of invocation.
  // If one invocation fails, no bid prices are returned, and the status of the
  // first failing invocation is returned.
  //
  // Blocks until all the outputs can be returned, or until a failure has been
  // detected.
  virtual absl::StatusOr<std::vector<Output>> BatchInvoke(
      const std::vector<Input>& bidding_function_inputs) const = 0;
};
}  // namespace function
}  // namespace aviary
#endif  // FUNCTION_BIDDING_FUNCTION_INTERFACE_H_H_
