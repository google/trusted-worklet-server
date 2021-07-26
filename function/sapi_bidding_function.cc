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

#include "function/sapi_bidding_function.h"

#include <asm/unistd_64.h>
#include <linux/prctl.h>
#include "absl/status/status.h"
#include "function/bidding_function_sandbox.pb.h"
#include "function/bidding_function_sapi_adapter.h"
#include "function/bidding_function_sapi_adapter_bin_embed.h"
#include "sandboxed_api/sandbox.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
#include "util/status_encoding.h"
#include "util/status_macros.h"

namespace aviary {
namespace function {
namespace {
using ::aviary::util::StatusFromProto;

constexpr absl::Duration kCompileTimeLimit = absl::Seconds(5);

template <typename Input>
BatchedInvocationInputs GetBatchedInvocationInputs(
    const std::vector<Input>& inputs) {
  BatchedInvocationInputs inputs_proto;
  for (const auto& input : inputs) {
    inputs_proto.add_inputs()->PackFrom(input);
  }
  return inputs_proto;
}

template <typename Input>
BiddingFunctionSpec::FunctionType GetFunctionType();

template <>
BiddingFunctionSpec::FunctionType GetFunctionType<BiddingFunctionInput>() {
  return BiddingFunctionSpec::FLEDGE_BIDDING_FUNCTION;
}

template <>
BiddingFunctionSpec::FunctionType GetFunctionType<AdScoringFunctionInput>() {
  return BiddingFunctionSpec::FLEDGE_AD_SCORING_FUNCTION;
}

template <typename Input>
BiddingFunctionSpec GetBiddingFunctionSpec(absl::string_view script_source,
                                           const FunctionOptions& options) {
  BiddingFunctionSpec spec;
  spec.set_bidding_function_source(std::string(script_source));
  spec.set_type(GetFunctionType<Input>());
  spec.set_flatten_function_arguments(options.flatten_function_arguments);
  return spec;
}
}  // namespace

template <typename Input, typename Output>
SapiBiddingFunction<Input, Output>::Sandbox::Sandbox()
    : ::sapi::Sandbox(bidding_function_sapi_adapter_bin_embed_create()) {}

// This policy provides the minimum permissions needed to run V8, ensuring
// that the sandbox is as secure as possible while still allowing the use
// of V8 to compile and execute JavaScript functions.
template <typename Input, typename Output>
std::unique_ptr<sandbox2::Policy>
SapiBiddingFunction<Input, Output>::Sandbox::ModifyPolicy(
    sandbox2::PolicyBuilder*) {
  // Return a new policy.
  return sandbox2::PolicyBuilder()
      .DisableNamespaces()
      .AllowRead()
      .AllowOpen()
      .AllowTCGETS()
      .AllowLogForwarding()
      .AllowGetPIDs()
      .AllowExit()
      .AllowStat()
      .AddPolicyOnSyscall(__NR_prctl,
                          {
                              ARG_32(0),
                              JEQ32(PR_SET_NAME, ALLOW),
                              KILL,
                          })
      .AllowSyscalls({
          // Allows to mark pages as executable, which necessary for V8 JIT to
          // work.
          __NR_mprotect,
          __NR_madvise,
          __NR_set_robust_list,
          __NR_sched_yield,
      })
      .BuildOrDie();
}

template <typename Input, typename Output>
absl::Status SapiBiddingFunction<Input, Output>::Sandbox::CompileFunction(
    const BiddingFunctionSpec& spec) {
  if (!comms()->SendTLV(static_cast<uint32_t>(SandboxedFunctionOp::kCompile),
                        /*length=*/0,
                        /*value=*/nullptr)) {
    return absl::InternalError("SendTLV failed");
  }
  if (!comms()->SendProtoBuf(spec)) {
    return absl::InternalError("SendProtoBuf failed");
  }
  absl::Status compilation_status;
  if (!comms()->RecvStatus(&compilation_status)) {
    return absl::InternalError("RecvStatus failed");
  }
  return compilation_status;
}

template <typename Input, typename Output>
absl::StatusOr<BatchedInvocationOutputs>
SapiBiddingFunction<Input, Output>::Sandbox::BatchExecute(
    const BatchedInvocationInputs& inputs) {
  // TODO(b/191545684): recycle the sandbox in all the failure cases
  if (!comms()->SendTLV(
          static_cast<uint32_t>(SandboxedFunctionOp::kBatchExecute),
          /*length=*/0,
          /*value=*/nullptr)) {
    return absl::InternalError("SendTLV failed");
  }
  if (!comms()->SendProtoBuf(inputs)) {
    return absl::InternalError("SendProtoBuf failed");
  }
  absl::Status invocation_status;
  if (!comms()->RecvStatus(&invocation_status)) {
    return absl::InternalError("RecvStatus failed");
  }
  if (!invocation_status.ok()) {
    return invocation_status;
  }
  BatchedInvocationOutputs outputs;
  if (!comms()->RecvProtoBuf(&outputs)) {
    return absl::InternalError("RecvProtoBuf failed");
  }
  return outputs;
}

template <typename Input, typename Output>
absl::StatusOr<std::unique_ptr<BiddingFunctionInterface<Input, Output>>>
SapiBiddingFunction<Input, Output>::Create(absl::string_view script_source,
                                           const FunctionOptions& options) {
  auto sandbox = std::make_unique<Sandbox>();
  RETURN_IF_ERROR(sandbox->Init());

  RETURN_IF_ERROR(sandbox->SetWallTimeLimit(kCompileTimeLimit));
  RETURN_IF_ERROR(sandbox->CompileFunction(
      GetBiddingFunctionSpec<Input>(script_source, options)));
  // Disarm the wall time limit until the next execution.
  RETURN_IF_ERROR(sandbox->SetWallTimeLimit(absl::ZeroDuration()));
  return absl::WrapUnique(new SapiBiddingFunction(std::move(sandbox), options));
}

template <typename Input, typename Output>
absl::StatusOr<std::vector<Output>>
SapiBiddingFunction<Input, Output>::BatchInvoke(
    const std::vector<Input>& bidding_function_inputs) const {
  RETURN_IF_ERROR(sandbox_->SetWallTimeLimit(execute_duration_limit_));
  absl::StatusOr<BatchedInvocationOutputs> status_or_outputs =
      sandbox_->BatchExecute(
          GetBatchedInvocationInputs<Input>(bidding_function_inputs));
  // Disarm the wall time limit until the next execution.
  RETURN_IF_ERROR(sandbox_->SetWallTimeLimit(absl::ZeroDuration()));
  ASSIGN_OR_RETURN(auto outputs_proto, status_or_outputs);
  std::vector<Output> outputs_vector;
  for (const auto& outputs_any : outputs_proto.outputs()) {
    if (!outputs_any.UnpackTo(&outputs_vector.emplace_back())) {
      return absl::InternalError("Unable to unpack the function outputs.");
    }
  }
  return outputs_vector;
}

template <typename Input, typename Output>
SapiBiddingFunction<Input, Output>::SapiBiddingFunction(
    std::unique_ptr<Sandbox> sandbox,
    const FunctionOptions& options)
    : sandbox_(std::move(sandbox)), options_(options) {}

template class SapiBiddingFunction<BiddingFunctionInput, BiddingFunctionOutput>;
template class SapiBiddingFunction<AdScoringFunctionInput,
                                   AdScoringFunctionOutput>;

}  // namespace function
}  // namespace aviary
