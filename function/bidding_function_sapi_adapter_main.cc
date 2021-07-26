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

// Sandboxee binary for the execution of bidding and scoring functions using
// Sandbox2
// (https://developers.google.com/sandboxed-api/docs/sandbox2/overview).
// This binary gets embedded into the main Aviary binary as data and runs as a
// child process spawned by the SapiBiddingFunction::Sandbox class with the help
// of Sandbox2, i.e.:
//
// auto sandbox = std::make_unique<Sandbox>();
// RETURN_IF_ERROR(sandbox->Init());
// //...operations on the sandbox

#include "function/bidding_function_sapi_adapter.h"
#include "google/rpc/status.pb.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/forkingclient.h"
#include "v8/v8_platform_initializer.h"

namespace aviary::function {
namespace {

// Serves a single request to the sandbox.
void ServeRequest(sandbox2::Comms& comms) {
  uint32_t tag;
  std::vector<uint8_t> unused;
  // Receive the tag of a command to be performed by the sandboxee.
  CHECK(comms.RecvTLV(&tag, &unused));
  switch (static_cast<SandboxedFunctionOp>(tag)) {
    case SandboxedFunctionOp::kCompile: {
      BiddingFunctionSpec spec;
      if (comms.RecvProtoBuf(&spec)) {
        absl::Status compilation_status = CompileFunction(spec);
        comms.SendStatus(compilation_status);
      } else {
        comms.SendStatus(absl::InvalidArgumentError("RecvProtoBuf failed"));
      }
    } break;
    case SandboxedFunctionOp::kBatchExecute: {
      BatchedInvocationInputs invocation_inputs;
      if (comms.RecvProtoBuf(&invocation_inputs)) {
        absl::StatusOr<BatchedInvocationOutputs> outputs_or =
            BatchExecuteFunction(invocation_inputs);
        if (comms.SendStatus(outputs_or.status()) && outputs_or.ok()) {
          comms.SendProtoBuf(outputs_or.value());
        }
      } else {
        comms.SendStatus(absl::InvalidArgumentError("RecvProtoBuf failed"));
      }
    } break;
    // Handle kMsgExit which can be triggered by sapi::Sandbox to terminate the
    // sandbox.
    case SandboxedFunctionOp::kMsgExit: {
      exit(0);
    }
  }
}
}  // namespace
}  // namespace aviary::function

// Sandbox main function that uses ForkServer to spawn sandboxee child processes
// that execute JavaScript functions.
//
// Adapted from
// https://github.com/google/sandboxed-api/blob/main/sandboxed_api/client.cc.
int main(int argc, char** argv) {
  sandbox2::Comms comms(sandbox2::Comms::kSandbox2ClientCommsFD);
  sandbox2::ForkingClient s2client{&comms};

  // Forkserver loop.
  while (true) {
    pid_t pid = s2client.WaitAndFork();
    if (pid == -1) {
      exit(1);
    }
    if (pid == 0) {
      break;
    }
  }

  aviary::v8::V8PlatformInitializer v8_platform_initializer;
  s2client.SandboxMeHere();

  // Run request serving loop.
  while (true) {
    aviary::function::ServeRequest(comms);
  }
}
