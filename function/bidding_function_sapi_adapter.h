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

#ifndef FUNCTION_BIDDING_FUNCTION_SAPI_ADAPTER_H_
#define FUNCTION_BIDDING_FUNCTION_SAPI_ADAPTER_H_

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "function/bidding_function_sandbox.pb.h"
#include "google/rpc/status.pb.h"

namespace aviary::function {

// Operations supported by the sandbox that executes a single JavaScript
// function.
enum class SandboxedFunctionOp {
  // Exits a sandbox.
  // Must match kMsgExit from
  // https://github.com/google/sandboxed-api/blob/main/sandboxed_api/call.h.
  // We reuse sapi::Sandbox for spawning and communicating with the sandbox, and
  // kMsgExit can be triggered by sapi::Sandbox to request the sandbox to exit.
  kMsgExit = 0x104,
  // Compile a function. Pre-requisite for subsequent function executions.
  // Each sandbox supports the execution of one function.
  kCompile = 0x1001,
  // Execute previously compiled function on a batch of inputs.
  kBatchExecute = 0x1002,
};

// Compiles and prepares a function for later execution within the current
// sandbox. Returns compilation status.
absl::Status CompileFunction(const BiddingFunctionSpec& spec);

// Executes the function that was previously compiled within the current sandbox
// for a batch of inputs. Returns a batch of outputs in the same order as the
// inputs or an error status.
absl::StatusOr<BatchedInvocationOutputs> BatchExecuteFunction(
    const BatchedInvocationInputs& invocation_inputs);
}  // namespace aviary::function

#endif // FUNCTION_BIDDING_FUNCTION_SAPI_ADAPTER_H_
