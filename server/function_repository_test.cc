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

#include "server/function_repository.h"

#include "function/bidding_function.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "v8/v8_platform_initializer.h"

namespace aviary::server {
namespace {
using ::aviary::function::FledgeAdScoringFunction;
using ::aviary::function::FledgeBiddingFunction;
using ::aviary::v8::V8PlatformInitializer;
using ::testing::ElementsAre;
using ::testing::Property;

TEST(FunctionRepositoryTest, GetBiddingFunctionSuccess) {
  V8PlatformInitializer v8_platform_initializer;
  absl::flat_hash_map<
      std::string, std::unique_ptr<::aviary::function::BiddingFunctionInterface<
                       BiddingFunctionInput, BiddingFunctionOutput>>>
      bidding_functions;
  bidding_functions.insert(
      {"local://bidding_function",
       FledgeBiddingFunction::Create(R"(input => ({bid: 1.23}))").value()});
  bidding_functions.insert(
      {"local://other_bidding_function",
       FledgeBiddingFunction::Create(R"(input => ({bid: 3.45}))").value()});
  FunctionRepository repository(std::move(bidding_functions), {});
  auto bidding_function_or =
      repository.GetBiddingFunction("local://bidding_function");
  ASSERT_TRUE(bidding_function_or.ok());
  BiddingFunctionInput input;
  auto result_or = bidding_function_or.value()->BatchInvoke({input});
  ASSERT_TRUE(result_or.ok());
  EXPECT_THAT(result_or.value(),
              ElementsAre(Property(&BiddingFunctionOutput::bid, 1.23)));

  bidding_function_or =
      repository.GetBiddingFunction("local://other_bidding_function");
  ASSERT_TRUE(bidding_function_or.ok());
  result_or = bidding_function_or.value()->BatchInvoke({input});
  ASSERT_TRUE(result_or.ok());
  EXPECT_THAT(result_or.value(),
              ElementsAre(Property(&BiddingFunctionOutput::bid, 3.45)));
}

TEST(FunctionRepositoryTest, GetBiddingFunctionNotFound) {
  FunctionRepository repository({}, {});
  auto bidding_function_or =
      repository.GetBiddingFunction("local://bidding_function");
  EXPECT_FALSE(bidding_function_or.ok());
  EXPECT_EQ(bidding_function_or.status().code(), absl::StatusCode::kNotFound)
      << bidding_function_or.status().ToString();
}

TEST(FunctionRepositoryTest, GetBiddingFunctionUnavailable) {
  absl::flat_hash_map<
      std::string, std::unique_ptr<::aviary::function::BiddingFunctionInterface<
                       BiddingFunctionInput, BiddingFunctionOutput>>>
      bidding_functions;
  bidding_functions.insert({"local://bidding_function", nullptr});
  FunctionRepository repository(std::move(bidding_functions), {});
  auto bidding_function_or =
      repository.GetBiddingFunction("local://bidding_function");
  EXPECT_FALSE(bidding_function_or.ok());
  EXPECT_EQ(bidding_function_or.status().code(), absl::StatusCode::kUnavailable)
      << bidding_function_or.status().ToString();
}

TEST(FunctionRepositoryTest, GetAdScoringFunctionSuccess) {
  V8PlatformInitializer v8_platform_initializer;
  absl::flat_hash_map<
      std::string, std::unique_ptr<::aviary::function::BiddingFunctionInterface<
                       AdScoringFunctionInput, AdScoringFunctionOutput>>>
      ad_scoring_functions;
  ad_scoring_functions.insert({"local://scoring_function",
                               FledgeAdScoringFunction::Create(
                                   R"(input => ({desirabilityScore: 42.0}))")
                                   .value()});
  ad_scoring_functions.insert({"local://other_scoring_function",
                               FledgeAdScoringFunction::Create(
                                   R"(input => ({desirabilityScore: 43.0}))")
                                   .value()});
  FunctionRepository repository({}, std::move(ad_scoring_functions));
  auto ad_scoring_function_or =
      repository.GetAdScoringFunction("local://scoring_function");
  ASSERT_TRUE(ad_scoring_function_or.ok());
  AdScoringFunctionInput input;
  auto result_or = ad_scoring_function_or.value()->BatchInvoke({input});
  ASSERT_TRUE(result_or.ok());
  EXPECT_THAT(result_or.value(),
              ElementsAre(Property(&AdScoringFunctionOutput::desirability_score,
                                   42.0)));

  ad_scoring_function_or =
      repository.GetAdScoringFunction("local://other_scoring_function");
  ASSERT_TRUE(ad_scoring_function_or.ok());
  result_or = ad_scoring_function_or.value()->BatchInvoke({input});
  ASSERT_TRUE(result_or.ok());
  EXPECT_THAT(result_or.value(),
              ElementsAre(Property(&AdScoringFunctionOutput::desirability_score,
                                   43.0)));
}

TEST(FunctionRepositoryTest, GetAdScoringFunctionNotFound) {
  FunctionRepository repository({}, {});
  auto bidding_function_or =
      repository.GetAdScoringFunction("local://scoring_function");
  EXPECT_FALSE(bidding_function_or.ok());
  EXPECT_EQ(bidding_function_or.status().code(), absl::StatusCode::kNotFound)
      << bidding_function_or.status().ToString();
}

TEST(FunctionRepositoryTest, GetAdScoringFunctionUnavailable) {
  absl::flat_hash_map<
      std::string, std::unique_ptr<::aviary::function::BiddingFunctionInterface<
                       AdScoringFunctionInput, AdScoringFunctionOutput>>>
      ad_scoring_functions;
  ad_scoring_functions.insert({"local://scoring_function", nullptr});
  FunctionRepository repository({}, std::move(ad_scoring_functions));
  auto bidding_function_or =
      repository.GetAdScoringFunction("local://scoring_function");
  EXPECT_FALSE(bidding_function_or.ok());
  EXPECT_EQ(bidding_function_or.status().code(), absl::StatusCode::kUnavailable)
      << bidding_function_or.status().ToString();
}
}  // namespace
}  // namespace aviary::server
