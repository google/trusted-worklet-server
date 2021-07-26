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

#ifndef FUNCTION_SAPI_BIDDING_FUNCTION_H_
#define FUNCTION_SAPI_BIDDING_FUNCTION_H_

#include "function/bidding_function_interface.h"
#include "function/bidding_function_sandbox.pb.h"
#include "sandboxed_api/sandbox.h"

namespace aviary::function {

// A single bidding function wrapped into a SAPI sandbox
// (https://developers.google.com/sandboxed-api) for the extra layer of security
// protection.
template <typename Input, typename Output>
class SapiBiddingFunction : public BiddingFunctionInterface<Input, Output> {
 public:
  static absl::StatusOr<
      std::unique_ptr<BiddingFunctionInterface<Input, Output>>>
  Create(absl::string_view script_source,
         const FunctionOptions& options = {
             .flatten_function_arguments = false});

  virtual ~SapiBiddingFunction() = default;

  absl::StatusOr<std::vector<Output>> BatchInvoke(
      const std::vector<Input>& bidding_function_inputs) const override;

  SapiBiddingFunction(const SapiBiddingFunction&) = delete;
  SapiBiddingFunction& operator=(const SapiBiddingFunction&) = delete;

 private:
  // Implements low-level requests to the sandbox for compiling and running
  // bidding functions.
  class Sandbox : public ::sapi::Sandbox {
   public:
    Sandbox();

    // Requests the sandboxee to compile a bidding function and returns a
    // status. Should only be invoked once per sandbox.
    absl::Status CompileFunction(const BiddingFunctionSpec& spec);

    // Requests the sandboxee to execute a bidding function for a batch of
    // inputs. Returns a outputs in the order corresponding to the order of
    // inputs or an error status.
    absl::StatusOr<BatchedInvocationOutputs> BatchExecute(
        const BatchedInvocationInputs& inputs);

   private:
    std::unique_ptr<sandbox2::Policy> ModifyPolicy(
        sandbox2::PolicyBuilder*) override;
  };

  explicit SapiBiddingFunction(std::unique_ptr<Sandbox> sandbox,
                               const FunctionOptions& options);

  std::unique_ptr<Sandbox> sandbox_;
  const FunctionOptions options_;
  // A fail-safe max duration to prevent a bidding function execution from
  // running indefinitely within the sandbox.
  absl::Duration execute_duration_limit_ = absl::Seconds(1);
};

using FledgeSapiBiddingFunction =
    SapiBiddingFunction<BiddingFunctionInput, BiddingFunctionOutput>;
using FledgeSapiAdScoringFunction =
    SapiBiddingFunction<AdScoringFunctionInput, AdScoringFunctionOutput>;

}  // namespace aviary::function

#endif  // FUNCTION_SAPI_BIDDING_FUNCTION_H_
