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

#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/status/status.h"
#include "grpc++/ext/proto_server_reflection_plugin.h"
#include "grpc++/grpc++.h"
#include "server/ad_auctions.h"
#include "v8.h"
#include "v8/v8_platform_initializer.h"

ABSL_FLAG(std::string,
          bind_address,
          "0.0.0.0:8080",
          "Server address to bind to.");

ABSL_FLAG(std::string,
          configuration_file,
          "",
          "Path to the configuration file in YAML format.");

using aviary::server::FunctionSource;
using grpc::Server;
using grpc::ServerBuilder;

void RunServer() {
  std::string server_address = absl::GetFlag(FLAGS_bind_address);
  FunctionSource source;
  auto service_or = aviary::server::AdAuctionsImpl::Create(
      source, absl::GetFlag(FLAGS_configuration_file));
  if (!service_or.ok()) {
    std::cerr << "Unable to initialize the server: "
              << service_or.status().ToString();
    return;
  }

  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();

  ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  int bound_port = 0;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials(),
                           &bound_port);
  builder.RegisterService(service_or.value().get());
  std::unique_ptr<Server> server(builder.BuildAndStart());
  if (bound_port != 0) {
    std::cout << "Server listening on " << server_address << std::endl;
    server->Wait();
  } else {
    std::cout << "Unable to bind to address " << server_address << ". Exiting."
              << std::endl;
    exit(1);
  }
}

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  // Initialize V8.
  aviary::v8::V8PlatformInitializer v8_platform_initializer;
  RunServer();
  return 0;
}
