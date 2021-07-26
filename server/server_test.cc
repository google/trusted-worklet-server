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

#include <fstream>
#include <thread>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/substitute.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "grpc++/grpc++.h"
#include "gtest/gtest.h"
#include "httplib.h"
#include "proto/aviary.grpc.pb.h"
#include "subprocess.hpp"
#include "util/parse_proto.h"
#include "util/unused_port.h"

namespace aviary {
namespace server {

using ::aviary::util::FindUnusedPort;
using ::aviary::util::ParseTextOrDie;

// Starts up an Aviary server in a separate process on a random local unused
// port.
class AviaryServer : public ::testing::Environment {
 public:
  static std::string ConfigurationFileName() {
    return absl::StrCat(testing::TempDir(), "/test_configuration.yaml");
  }

  static std::string WriteYamlConfiguration(absl::string_view yaml_source) {
    std::string configuration_filename = ConfigurationFileName();
    std::ofstream configuration_file;
    configuration_file.open(configuration_filename);
    configuration_file << yaml_source;
    configuration_file.close();
    return configuration_filename;
  }

  void StartStaticResourcesServer() {
    static_resources_port_ = FindUnusedPort().value();
    absl::Notification server_ready;
    static_resources_server_thread_ =
        std::make_unique<std::thread>([this, &server_ready] {
          static_resources_server_.bind_to_port("0.0.0.0",
                                                static_resources_port_);
          server_ready.Notify();
          static_resources_server_.listen_after_bind();
        });
    ASSERT_TRUE(server_ready.WaitForNotificationWithTimeout(absl::Seconds(5)));
    static_resources_server_.Get("/doubling.js", [&](const httplib::Request&,
                                                     httplib::Response& res) {
      res.set_content(
          R"((function(interestGroup, auctionSignals, perBuyerSignals, trustedBiddingSignals, browserSignals) { return { bid: perBuyerSignals.model.contextualCpm * 2.0 }; }))",
          "text/javascript");
    });
  }

  void StopStaticResourcesServer() {
    static_resources_server_.stop();
    static_resources_server_thread_->join();
  }

  void StartGrpcServer() {
    std::string test_workspace_dir =
        absl::StrCat(std::string(getenv("TEST_SRCDIR")), "/",
                     std::string(getenv("TEST_WORKSPACE")));
    std::string server_binary =
        absl::StrCat(test_workspace_dir, "/server/server");
    address_ = absl::StrCat("0.0.0.0:", FindUnusedPort().value());
    server_process_ =
        subprocess::RunBuilder(
            {server_binary, absl::StrCat("--bind_address=", address_),
             absl::StrCat("--configuration_file=", ConfigurationFileName())})
            .popen();
    ABSL_ASSERT(WaitUntilServerIsReady());
  }

  void StopGrpcServer() { server_process_.kill(); }

  void SetUp() override {
    StartStaticResourcesServer();
    WriteYamlConfiguration(absl::Substitute(R"(
biddingFunctions:
- uri: local://constant
  source: |
    inputs => ({ bid: 42.0 })
- uri: $0
adScoringFunctions: []
)",
                                            StaticResourceUrl("doubling.js")));
    StartGrpcServer();
  }

  void TearDown() override {
    StopGrpcServer();
    StopStaticResourcesServer();
  }

  std::string StaticResourceUrl(absl::string_view path) const {
    return absl::Substitute("http://localhost:$0/$1", static_resources_port_,
                            path);
  }

  std::string Address() const { return address_; }

 private:
  // Waits until server under test is ready to accept connections.
  // Returns true if the server is accepting connections.
  bool WaitUntilServerIsReady() const {
    int retries = 10;
    do {
      grpc::ChannelArguments channel_args;
      auto channel =
          grpc::CreateChannel(Address(), grpc::InsecureChannelCredentials());
      if (channel->WaitForConnected(std::chrono::system_clock::now() +
                                    std::chrono::milliseconds(200))) {
        return true;
      }
    } while (--retries > 0);
    return false;
  }

  std::string address_;
  subprocess::Popen server_process_;
  httplib::Server static_resources_server_;
  int static_resources_port_ = 0;
  std::unique_ptr<std::thread> static_resources_server_thread_;
};

// Creates a single environment per T and register as a GlobalTestEnvironment.
template <typename T>
T* GetEnv() {
  static T* const instance = [] {
    T* instance = new T;
    ::testing::AddGlobalTestEnvironment(instance);
    return instance;
  }();
  return instance;
}

AviaryServer* const aviary_server = GetEnv<AviaryServer>();

class ServerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    stub_ = AdAuctions::NewStub(grpc::CreateChannel(
        GetEnv<AviaryServer>()->Address(), grpc::InsecureChannelCredentials()));
  }

  absl::StatusOr<BiddingFunctionOutput> ComputeBid(
      const ComputeBidRequest& request) {
    ::grpc::ClientContext client_context;
    BiddingFunctionOutput response;
    const grpc::Status status =
        stub_->ComputeBid(&client_context, request, &response);
    if (!status.ok()) {
      return absl::Status(static_cast<absl::StatusCode>(status.error_code()),
                          status.error_message());
    }
    return response;
  }

  std::unique_ptr<AdAuctions::Stub> stub_;
};

TEST_F(ServerTest, HappyPath) {
  // Invoke a bidding function downloaded from a remote URL.
  auto request = ParseTextOrDie<ComputeBidRequest>(absl::Substitute(
      R"pb(
        bidding_function_name: "$0"
        input {
          per_buyer_signals {
            fields {
              key: "model"
              value: {
                struct_value: {
                  fields {
                    key: "contextualCpm"
                    value: { number_value: 1.23 }
                  }
                }
              }
            }
          }
        }
      )pb",
      GetEnv<AviaryServer>()->StaticResourceUrl("doubling.js")));
  auto response = ComputeBid(request);
  EXPECT_TRUE(response.ok());
  EXPECT_EQ(response.value().bid(), 2.46);

  // Invoke a different bidding function to verify dispatching works.
  request = ParseTextOrDie<ComputeBidRequest>(
      R"pb(
        bidding_function_name: "local://constant"
      )pb");
  response = ComputeBid(request);
  EXPECT_TRUE(response.ok());
  EXPECT_EQ(response.value().bid(), 42.0);
}

TEST_F(ServerTest, InvocationError) {
  auto request = ParseTextOrDie<ComputeBidRequest>(absl::Substitute(
      R"pb(
        bidding_function_name: "$0"
        input {
          auction_signals {
            fields {
              key: "foo"
              value: { number_value: 1.23 }
            }
          }
        }
      )pb",
      GetEnv<AviaryServer>()->StaticResourceUrl("doubling.js")));
  auto response = ComputeBid(request);
  EXPECT_EQ(response.status().code(), absl::StatusCode::kInternal);
}

TEST_F(ServerTest, UnknownFunction) {
  auto request = ParseTextOrDie<ComputeBidRequest>(
      R"pb(
        bidding_function_name: "unknown"
        input {
          per_buyer_signals {
            fields {
              key: "foo"
              value: { number_value: 1.23 }
            }
          }
        }
      )pb");
  auto response = ComputeBid(request);
  EXPECT_EQ(response.status().code(), absl::StatusCode::kNotFound);
}

}  // namespace server
}  // namespace aviary
