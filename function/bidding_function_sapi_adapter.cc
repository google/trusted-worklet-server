// Copyright 2021 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/synchronization/mutex.h"
#include "function/bidding_function.h"
#include "function/bidding_function_interface.h"
#include "function/bidding_function_sandbox.pb.h"
#include "util/status_encoding.h"
#include "util/status_macros.h"

namespace aviary {
namespace function {
namespace {

using ::aviary::util::SaveStatusToProto;
using ::google::protobuf::Message;

ABSL_CONST_INIT absl::Mutex bidding_function_mutex(absl::kConstInit);
// Sandbox-wide instance of a function.
absl::variant<
    BiddingFunctionInterface<BiddingFunctionInput, BiddingFunctionOutput>*,
    BiddingFunctionInterface<AdScoringFunctionInput, AdScoringFunctionOutput>*>
    single_bidding_function ABSL_GUARDED_BY(bidding_function_mutex);
BiddingFunctionSpec::FunctionType function_type ABSL_GUARDED_BY(
    bidding_function_mutex) = BiddingFunctionSpec::FUNCTION_TYPE_UNSPECIFIED;

template <typename Output>
BatchedInvocationOutputs GetFunctionOutputs(
    const std::vector<Output>& invoke_result) {
  BatchedInvocationOutputs function_outputs;
  for (const auto& output : invoke_result) {
    function_outputs.mutable_outputs()->Add()->PackFrom(output);
  }
  return function_outputs;
}

template <typename Input, typename Output>
absl::Status DoCompileFunction(const BiddingFunctionSpec& spec) {
  using Function = BiddingFunction<Input, Output>;
  ASSIGN_OR_RETURN(
      auto bidding_function,
      Function::Create(spec.bidding_function_source(),
                       FunctionOptions{.flatten_function_arguments =
                                           spec.flatten_function_arguments()}));
  {
    absl::MutexLock lock(&bidding_function_mutex);
    if (function_type != BiddingFunctionSpec::FUNCTION_TYPE_UNSPECIFIED) {
      return absl::FailedPreconditionError(
          "Function has already been initialized within the sandbox.");
    }
    single_bidding_function = bidding_function.release();
    function_type = spec.type();
  }
  return absl::OkStatus();
}

template <typename Input, typename Output>
absl::StatusOr<BatchedInvocationOutputs> DoBatchExecuteFunction(
    const BatchedInvocationInputs& invocation_inputs) {
  const BiddingFunctionInterface<Input, Output>* bidding_function;
  {
    absl::ReaderMutexLock lock(&bidding_function_mutex);
    bidding_function = absl::get<BiddingFunctionInterface<Input, Output>*>(
        single_bidding_function);
  }
  std::vector<Input> inputs_vector;
  for (const auto& inputs_any : invocation_inputs.inputs()) {
    if (!inputs_any.UnpackTo(&inputs_vector.emplace_back())) {
      return absl::InvalidArgumentError("Unable to unpack inputs");
    }
  }
  ASSIGN_OR_RETURN(auto invocation_result,
                   bidding_function->BatchInvoke(inputs_vector));
  return GetFunctionOutputs(invocation_result);
}
}  // namespace

absl::Status CompileFunction(const BiddingFunctionSpec& spec) {
  switch (spec.type()) {
    case BiddingFunctionSpec::FLEDGE_BIDDING_FUNCTION:
      return DoCompileFunction<BiddingFunctionInput, BiddingFunctionOutput>(
          spec);
    case BiddingFunctionSpec::FLEDGE_AD_SCORING_FUNCTION:
      return DoCompileFunction<AdScoringFunctionInput, AdScoringFunctionOutput>(
          spec);
    default:
      return absl::InvalidArgumentError(
          absl::StrCat("Unexpected function type: ",
                       BiddingFunctionSpec::FunctionType_Name(spec.type())));
  }
}

absl::StatusOr<BatchedInvocationOutputs> BatchExecuteFunction(
    const BatchedInvocationInputs& invocation_inputs) {
  BiddingFunctionSpec::FunctionType type;
  {
    absl::ReaderMutexLock lock(&bidding_function_mutex);
    if (function_type == BiddingFunctionSpec::FUNCTION_TYPE_UNSPECIFIED) {
      return absl::FailedPreconditionError(
          "Function has not been initialized within the sandbox.");
    }
    type = function_type;
  }
  switch (type) {
    case BiddingFunctionSpec::FLEDGE_BIDDING_FUNCTION:
      return DoBatchExecuteFunction<BiddingFunctionInput,
                                    BiddingFunctionOutput>(invocation_inputs);
    case BiddingFunctionSpec::FLEDGE_AD_SCORING_FUNCTION:
      return DoBatchExecuteFunction<AdScoringFunctionInput,
                                    AdScoringFunctionOutput>(invocation_inputs);
    default:
      return absl::InvalidArgumentError(
          absl::StrCat("Unexpected function type: ",
                       BiddingFunctionSpec::FunctionType_Name(type)));
  }
}
}  // namespace function
}  // namespace aviary
