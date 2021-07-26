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

#include "server/function_source.h"

#include <thread>

#include "absl/strings/substitute.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "gtest/gtest.h"
#include "httplib.h"
#include "util/unused_port.h"

namespace aviary {
namespace server {
namespace {

using ::aviary::util::FindUnusedPort;

constexpr absl::string_view kSourceCode1 = R"(() => return "hello, world";)";
constexpr absl::string_view kSourceCode2 = R"(() => return "goodbye, world";)";

TEST(FunctionSourceTest, LocalSpecification) {
  FunctionSource source;
  absl::StatusOr<std::string> code_or = source.GetFunctionCode(
      {.uri = "local://a-function", .source_code = std::string(kSourceCode1)});
  EXPECT_TRUE(code_or.ok());
  EXPECT_EQ(code_or.value(), kSourceCode1);
}

TEST(FunctionSourceTest, LocalSpecificationMissingSource) {
  FunctionSource source;
  absl::StatusOr<std::string> code_or =
      source.GetFunctionCode({.uri = "local://a-function"});
  EXPECT_FALSE(code_or.ok());
  EXPECT_EQ(code_or.status().code(), absl::StatusCode::kInvalidArgument)
      << code_or.status().ToString();
}

TEST(FunctionSourceTest, BadSpecification) {
  FunctionSource source;
  absl::StatusOr<std::string> code_or = source.GetFunctionCode({});
  EXPECT_FALSE(code_or.ok());
  EXPECT_EQ(code_or.status().code(), absl::StatusCode::kInvalidArgument)
      << code_or.status().ToString();
}

class UrlFunctionSourceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    port_ = FindUnusedPort().value();
    absl::Notification server_ready;
    server_thread_ = std::make_unique<std::thread>([this, &server_ready] {
      ASSERT_TRUE(server_.bind_to_port("0.0.0.0", port_));
      server_ready.Notify();
      server_.listen_after_bind();
    });
    ASSERT_TRUE(server_ready.WaitForNotificationWithTimeout(absl::Seconds(5)));
    while (!server_.is_running()) {
      absl::SleepFor(absl::Milliseconds(10));
    }
  }

  void TearDown() override {
    server_.stop();
    server_thread_->join();
  }

  httplib::Server server_;
  int port_ = 0;
  std::unique_ptr<std::thread> server_thread_;
};

TEST_F(UrlFunctionSourceTest, HappyPath) {
  server_.Get("/source1.js",
              [&](const httplib::Request&, httplib::Response& res) {
                res.set_content(std::string(kSourceCode1), "text/javascript");
              });
  server_.Get("/source2.js",
              [&](const httplib::Request&, httplib::Response& res) {
                res.set_content(std::string(kSourceCode2), "text/javascript");
              });
  FunctionSource source;
  absl::StatusOr<std::string> code_or = source.GetFunctionCode(
      {.uri = absl::Substitute("http://localhost:$0/source1.js", port_)});
  EXPECT_TRUE(code_or.ok());
  EXPECT_EQ(code_or.value(), kSourceCode1);
  code_or = source.GetFunctionCode(
      {.uri = absl::Substitute("http://localhost:$0/source2.js", port_)});
  EXPECT_TRUE(code_or.ok());
  EXPECT_EQ(code_or.value(), kSourceCode2);
}

TEST_F(UrlFunctionSourceTest, NotFound) {
  FunctionSource source;
  absl::StatusOr<std::string> code_or = source.GetFunctionCode(
      {.uri = absl::Substitute("http://localhost:$0/source2.js", port_)});
  EXPECT_FALSE(code_or.ok());
  EXPECT_EQ(code_or.status().code(), absl::StatusCode::kNotFound)
      << code_or.status().ToString();
}

TEST_F(UrlFunctionSourceTest, BadUrl) {
  FunctionSource source;
  absl::StatusOr<std::string> code_or =
      source.GetFunctionCode({.uri = "bad-url"});
  EXPECT_FALSE(code_or.ok());
  EXPECT_EQ(code_or.status().code(), absl::StatusCode::kInvalidArgument)
      << code_or.status().ToString();
  code_or = source.GetFunctionCode({.uri = "#@!$%:"});
  EXPECT_FALSE(code_or.ok());
  EXPECT_EQ(code_or.status().code(), absl::StatusCode::kInvalidArgument)
      << code_or.status().ToString();
}

TEST_F(UrlFunctionSourceTest, BadRequest) {
  server_.Get("/source1.js", [&](const httplib::Request&,
                                 httplib::Response& res) { res.status = 400; });
  FunctionSource source;
  absl::StatusOr<std::string> code_or = source.GetFunctionCode(
      {.uri = absl::Substitute("http://localhost:$0/source1.js", port_)});
  EXPECT_FALSE(code_or.ok());
  EXPECT_EQ(code_or.status().code(), absl::StatusCode::kInvalidArgument)
      << code_or.status().ToString();
}

TEST_F(UrlFunctionSourceTest, PermissionDenied) {
  server_.Get("/source1.js", [&](const httplib::Request&,
                                 httplib::Response& res) { res.status = 403; });
  server_.Get("/source2.js", [&](const httplib::Request&,
                                 httplib::Response& res) { res.status = 401; });
  FunctionSource source;
  absl::StatusOr<std::string> code_or = source.GetFunctionCode(
      {.uri = absl::Substitute("http://localhost:$0/source1.js", port_)});
  EXPECT_FALSE(code_or.ok());
  EXPECT_EQ(code_or.status().code(), absl::StatusCode::kPermissionDenied)
      << code_or.status().ToString();
  code_or = source.GetFunctionCode(
      {.uri = absl::Substitute("http://localhost:$0/source2.js", port_)});
  EXPECT_FALSE(code_or.ok());
  EXPECT_EQ(code_or.status().code(), absl::StatusCode::kPermissionDenied)
      << code_or.status().ToString();
}
}  // namespace
}  // namespace server
}  // namespace aviary
