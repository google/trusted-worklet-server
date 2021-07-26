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

#ifndef FUNCTION_BIDDING_FUNCTION_H_
#define FUNCTION_BIDDING_FUNCTION_H_

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "function/bidding_function_interface.h"
#include "proto/bidding_function.pb.h"
#include "v8.h"

namespace aviary {
namespace function {
template <typename Input, typename Output>
class BiddingFunction;
namespace internal {
// Manages the lifetime of v8::Isolate.
class IsolateHolder {
  IsolateHolder(const IsolateHolder&) = delete;
  IsolateHolder(IsolateHolder&& other);

 private:
  template <typename Input, typename Output>
  friend class ::aviary::function::BiddingFunction;
  explicit IsolateHolder(v8::Isolate* isolate);
  ~IsolateHolder();

  using IsolatePtr = v8::Isolate*;
  operator IsolatePtr();  // NOLINT
  v8::Isolate* isolate_;
};
}  // namespace internal

// JavaScript function that executes the sandboxed bidding logic.
template <typename Input, typename Output>
class BiddingFunction : public BiddingFunctionInterface<Input, Output> {
 public:
  // Creates a bidding function given the JavaScript source that defines and
  // returns such function as a result of the execution. Error status may be
  // returned if the script that defines the bidding function does not compile
  // or does not run successfully, or if it does not return function upon
  // running.
  static absl::StatusOr<
      std::unique_ptr<BiddingFunctionInterface<Input, Output>>>
  Create(absl::string_view script_source,
         const FunctionOptions& options = {
             .flatten_function_arguments = false});

  absl::StatusOr<std::vector<Output>> BatchInvoke(
      const std::vector<Input>& bidding_function_inputs) const override;

  BiddingFunction(const BiddingFunction&) = delete;
  BiddingFunction(BiddingFunction&& other) = delete;

 private:
  explicit BiddingFunction(std::string startup_internal_data,
                           const FunctionOptions& options);

  static std::string GetFunctionDeclarationName();

  const FunctionOptions options_;
  std::unique_ptr<v8::ArrayBuffer::Allocator> allocator_;
  // v8::StartupData does not own `data` pointer; owned by
  // `startup_internal_data_` instead so it doesn't have to be manually deleted.
  // They essentially contain the "warmed-up" bidding function. A "warmed-up"
  // bidding function has been run, compiled, and invoked several times in
  // order to reduce latency for future invocations.
  const std::string startup_internal_data_;
  v8::StartupData startup_data_;
  mutable internal::IsolateHolder isolate_;
};

using FledgeBiddingFunction =
    BiddingFunction<BiddingFunctionInput, BiddingFunctionOutput>;
using FledgeAdScoringFunction =
    BiddingFunction<AdScoringFunctionInput, AdScoringFunctionOutput>;

}  // namespace function
}  // namespace aviary
#endif  // FUNCTION_BIDDING_FUNCTION_H_
