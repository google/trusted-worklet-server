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

#include "server/ad_auctions.h"

#include <fstream>
#include <iostream>

#include "absl/random/random.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "grpc++/grpc++.h"
#include "gtest/gtest.h"
#include "proto/aviary.grpc.pb.h"
#include "proto/aviary.pb.h"
#include "util/parse_proto.h"
#include "util/test_periodic_function.h"
#include "v8/v8_platform_initializer.h"

namespace aviary {
namespace server {
namespace {

using ::aviary::ComputeBidRequest;
using ::aviary::util::ParseTextOrDie;
using ::aviary::util::TestPeriodicFunctionContainer;
using ::aviary::v8::V8PlatformInitializer;
using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Property;
using Scored = ScoredInterestGroupBid;

constexpr absl::string_view kDoublingBiddingFunction = R"(
(interestGroup, auctionSignals, perBuyerSignals, trustedBiddingSignals, browserSignals) => ({ bid: perBuyerSignals.foo * 2,
            renderUrl: interestGroup.ads[0].renderUrl,
            ad: interestGroup.ads[0].adMetadata }))";

constexpr absl::string_view kTriplingBiddingFunction = R"(
(interestGroup, auctionSignals, perBuyerSignals, trustedBiddingSignals, browserSignals) => ({ bid: perBuyerSignals.foo * 3,
            renderUrl: interestGroup.ads[0].renderUrl,
            ad: interestGroup.ads[0].adMetadata }))";

constexpr absl::string_view kPreferFunnyAdsScoringFunction = R"(
(adMetadata, bid, auctionConfig, trustedScoringSignals, browserSignals) => ({ desirabilityScore: adMetadata && adMetadata.funny ? bid * 2 : bid }))";

constexpr absl::string_view kEngagementMultiplicationBiddingFunction = R"(
(interestGroup, auctionSignals, perBuyerSignals, trustedBiddingSignals, browserSignals) => ({ bid: perBuyerSignals.foo * interestGroup.userBiddingSignals.engagement,
            renderUrl: interestGroup.ads[0].renderUrl,
            ad: interestGroup.ads[0].adMetadata}))";

constexpr absl::string_view kFilterJokesAdCategoryScoringFunction =
    R"(
(adMetadata, bid, auctionConfig, trustedScoringSignals, browserSignals) => ({ desirabilityScore: adMetadata.categories.includes("jokes") ? 0 : bid }))";

constexpr absl::string_view kFilterJokesTrustedSignalsCategoryScoringFunction =
    R"(
(adMetadata, bid, auctionConfig, trustedScoringSignals, browserSignals) => ({ desirabilityScore: trustedScoringSignals.categories.includes("jokes") ? 0 : bid }))";

constexpr absl::string_view kFailingBiddingFunction = R"(
(interestGroup, auctionSignals, perBuyerSignals, trustedBiddingSignals, browserSignals) => ({ bid: 1000 + perBuyerSignals.foo.bar.baz,
            renderUrl: interestGroup.ads[0].renderUrl,
            ad: interestGroup.ads[0].adMetadata }))";

constexpr absl::string_view kFailingScoringFunction = R"(
(adMetadata, bid, auctionConfig, trustedScoringSignals, browserSignals) => ({ desirabilityScore: adMetadata.funny.bar.baz * 5 }))";

class TestFunctionSource : public FunctionSource {
 public:
  absl::StatusOr<std::string> GetFunctionCode(
      const FunctionSpecification& specification) const override {
    if (specification.source_code) {
      return *specification.source_code;
    }
    auto item = uri_function_store_.find(specification.uri);
    if (item == uri_function_store_.end()) {
      return absl::NotFoundError("Resource not found");
    }
    return item->second;
  }

  TestFunctionSource& AddRemoteFunction(absl::string_view uri,
                                        absl::string_view source_code) {
    uri_function_store_[uri] = source_code;
    return *this;
  }

 private:
  absl::flat_hash_map<std::string, std::string> uri_function_store_;
};
}  // namespace

class AdAuctionsTest : public ::testing::Test {
 protected:
  static std::string TempFileName() {
    absl::BitGen bitgen;
    return absl::StrCat(testing::TempDir(), "/",
                        absl::Uniform<uint32_t>(bitgen));
  }

  static std::string WriteYamlConfiguration(absl::string_view yaml_source) {
    std::string configuration_filename = TempFileName();
    std::ofstream configuration_file;
    configuration_file.open(configuration_filename);
    configuration_file << yaml_source;
    configuration_file.close();
    return configuration_filename;
  }

  std::string WriteStandardAuctionConfiguration() {
    function_source_
        .AddRemoteFunction("https://ssp.example/auction/preferFunnyAds.js",
                           kPreferFunnyAdsScoringFunction)
        .AddRemoteFunction("https://ssp.example/auction/preferBoringAds.js",
                           kFilterJokesAdCategoryScoringFunction)
        .AddRemoteFunction(
            "https://ssp.example/auction/preferBoringAdsFromTrustedSignals.js",
            kFilterJokesTrustedSignalsCategoryScoringFunction)
        .AddRemoteFunction(
            "https://ssp.example/auction/failingScoringFunction.js",
            kFailingScoringFunction)
        .AddRemoteFunction("https://adnetwork.example/bidding/double.js",
                           kDoublingBiddingFunction)
        .AddRemoteFunction("https://adnetwork.example/bidding/triple.js",
                           kTriplingBiddingFunction)
        .AddRemoteFunction("https://dsp.example/bidding/multiply.js",
                           kEngagementMultiplicationBiddingFunction)
        .AddRemoteFunction(
            "https://dsp.example/bidding/failingBiddingFunction.js",
            kFailingBiddingFunction);
    std::string configuration_filename = WriteYamlConfiguration(R"(
biddingFunctions:
  - uri: https://adnetwork.example/bidding/double.js
  - uri: https://adnetwork.example/bidding/triple.js
  - uri: https://dsp.example/bidding/multiply.js
  - uri: https://dsp.example/bidding/failingBiddingFunction.js
adScoringFunctions:
  - uri: https://ssp.example/auction/preferFunnyAds.js
  - uri: https://ssp.example/auction/preferBoringAds.js
  - uri: https://ssp.example/auction/preferBoringAdsFromTrustedSignals.js
  - uri: https://ssp.example/auction/failingScoringFunction.js
)");
    return configuration_filename;
  }

  std::unique_ptr<AdAuctions::Service> CreateAdAuctions(
      const Configuration& configuration) {
    return AdAuctionsImpl::Create(configuration, function_source_,
                                  refresh_periodic_functions_.Factory())
        .value();
  }

  TestFunctionSource function_source_;
  TestPeriodicFunctionContainer refresh_periodic_functions_;

 private:
  V8PlatformInitializer v8_platform_initializer;
};

TEST_F(AdAuctionsTest, ComputeBidHappyPath) {
  std::unique_ptr<AdAuctions::Service> ad_auctions =
      CreateAdAuctions(Configuration{
          .bidding_function_specs = {
              FunctionSpecification{
                  .uri = "local://double",
                  .source_code =
                      "(interestGroup, auctionSignals, perBuyerSignals, "
                      "trustedBiddingSignals, browserSignals) => ({ bid: "
                      "perBuyerSignals.foo * 2})"},
              FunctionSpecification{
                  .uri = "local://triple",
                  .source_code =
                      "(interestGroup, auctionSignals, perBuyerSignals, "
                      "trustedBiddingSignals, browserSignals) => ({ bid: "
                      "perBuyerSignals.foo * 3})"}}});
  auto request = ParseTextOrDie<ComputeBidRequest>(
      R"pb(
        bidding_function_name: "local://double"
        input {
          per_buyer_signals {
            fields {
              key: "foo"
              value { number_value: 21 }
            }
          }
        }
      )pb");
  ::aviary::BiddingFunctionOutput response;
  grpc::Status status =
      ad_auctions->ComputeBid(/*context=*/nullptr, &request, &response);
  ASSERT_TRUE(status.ok());
  EXPECT_EQ(response.bid(), 42.0);

  // Verify that the call gets dispatched to the correct bidding function.
  request.set_bidding_function_name("local://triple");
  status = ad_auctions->ComputeBid(/*context=*/nullptr, &request, &response);
  ASSERT_TRUE(status.ok());
  EXPECT_EQ(response.bid(), 63.0);
}

TEST_F(AdAuctionsTest, ComputeBidFunctionReload) {
  function_source_.AddRemoteFunction(
      "https://dsp.example/bidding/double.js",
      R"((interestGroup, auctionSignals, perBuyerSignals, trustedBiddingSignals, browserSignals) => ({ bid: perBuyerSignals.foo * 2}))");
  std::unique_ptr<AdAuctions::Service> ad_auctions = CreateAdAuctions(
      Configuration{.bidding_function_specs = {FunctionSpecification{
                        .uri = "https://dsp.example/bidding/double.js"}}});
  auto request = ParseTextOrDie<ComputeBidRequest>(
      R"pb(
        bidding_function_name: "https://dsp.example/bidding/double.js"
        input {
          per_buyer_signals {
            fields {
              key: "foo"
              value { number_value: 21 }
            }
          }
        }
      )pb");
  ::aviary::BiddingFunctionOutput response;
  grpc::Status status =
      ad_auctions->ComputeBid(/*context=*/nullptr, &request, &response);
  ASSERT_TRUE(status.ok());
  EXPECT_EQ(response.bid(), 42.0);
  // Function updated externally, but the refresh did not yet happen.
  function_source_.AddRemoteFunction(
      "https://dsp.example/bidding/double.js",
      R"((interestGroup, auctionSignals, perBuyerSignals, trustedBiddingSignals, browserSignals) => ({ bid: perBuyerSignals.foo * 3.0}))");
  status = ad_auctions->ComputeBid(/*context=*/nullptr, &request, &response);
  ASSERT_TRUE(status.ok());
  EXPECT_EQ(response.bid(), 42.0);
  // Force a refresh of all bidding and ad scoring functions.
  refresh_periodic_functions_.InvokeAllNow();
  status = ad_auctions->ComputeBid(/*context=*/nullptr, &request, &response);
  ASSERT_TRUE(status.ok());
  EXPECT_EQ(response.bid(), 63.0);
}

TEST_F(AdAuctionsTest, ComputeBidCreateFromConfigurationFile) {
  std::string configuration_filename = WriteYamlConfiguration(R"(
biddingFunctions:
  - uri: local://double
    source: |
      (interestGroup, auctionSignals, perBuyerSignals, trustedBiddingSignals, browserSignals) => ({ bid: perBuyerSignals.foo * 2 })
  - uri: local://triple
    source: |
      (function(interestGroup, auctionSignals, perBuyerSignals, trustedBiddingSignals, browserSignals) { return { bid: perBuyerSignals.foo * 3 }; })
adScoringFunctions: []
)");
  std::unique_ptr<AdAuctions::Service> ad_auctions =
      AdAuctionsImpl::Create(function_source_, configuration_filename).value();
  auto request = ParseTextOrDie<ComputeBidRequest>(
      R"pb(
        bidding_function_name: "local://double"
        input {
          per_buyer_signals {
            fields {
              key: "foo"
              value { number_value: 21 }
            }
          }
        }
      )pb");
  ::aviary::BiddingFunctionOutput response;
  grpc::Status status =
      ad_auctions->ComputeBid(/*context=*/nullptr, &request, &response);
  ASSERT_TRUE(status.ok());
  EXPECT_EQ(response.bid(), 42.0);

  // Verify that the call gets dispatched to the correct bidding function.
  request.set_bidding_function_name("local://triple");
  status = ad_auctions->ComputeBid(/*context=*/nullptr, &request, &response);
  ASSERT_TRUE(status.ok());
  EXPECT_EQ(response.bid(), 63.0);
}

TEST_F(AdAuctionsTest, RunAdAuctionHappyPath) {
  std::unique_ptr<AdAuctions::Service> ad_auctions =
      AdAuctionsImpl::Create(function_source_,
                             WriteStandardAuctionConfiguration(),
                             refresh_periodic_functions_.Factory())
          .value();
  auto request = ParseTextOrDie<RunAdAuctionRequest>(
      R"pb(
        interest_groups {
          owner: "adnetwork.example"
          name: "funnytoons"
          bidding_logic_url: "https://adnetwork.example/bidding/double.js"
          ads {
            render_url: "https://adnetwork.example/funny"
            ad_metadata {
              fields {
                key: "funny"
                value { bool_value: true }
              }
            }
          }
        }
        interest_groups {
          owner: "dsp.example"
          name: "boringreads"
          bidding_logic_url: "https://dsp.example/bidding/multiply.js"
          ads { render_url: "https://dsp.example/boringreads" }
          user_bidding_signals {
            fields {
              key: "engagement"
              value { number_value: 3 }
            }
          }
        }
        interest_groups {
          owner: "dsp.example"
          name: "ufoconspiracies"
          bidding_logic_url: "https://dsp.example/bidding/multiply.js"
          ads { render_url: "https://dsp.example/ufoconspiracies" }
          user_bidding_signals {
            fields {
              key: "engagement"
              value { number_value: 3.5 }
            }
          }
        }
        auction_configuration {
          decision_logic_url: "https://ssp.example/auction/preferFunnyAds.js"
          interest_group_buyers: [ "dsp.example", "adnetwork.example" ]
          per_buyer_signals {
            key: "adnetwork.example"
            value {
              fields {
                key: "foo"
                value { number_value: 21 }
              }
            }
          }
          per_buyer_signals {
            key: "dsp.example"
            value {
              fields {
                key: "foo"
                value { number_value: 20 }
              }
            }
          }
        }
      )pb");
  ::aviary::RunAdAuctionResponse response;
  grpc::Status status =
      ad_auctions->RunAdAuction(/*context=*/nullptr, &request, &response);
  ASSERT_TRUE(status.ok());
  EXPECT_TRUE(response.has_winning_bid());
  const auto& winning_bid = response.winning_bid();
  EXPECT_EQ(winning_bid.interest_group_owner(), "adnetwork.example");
  EXPECT_EQ(winning_bid.interest_group_name(), "funnytoons");
  EXPECT_EQ(winning_bid.render_url(), "https://adnetwork.example/funny");
  EXPECT_EQ(winning_bid.bid_price(), 42.0);
  EXPECT_EQ(winning_bid.desirability_score(), 84.0);

  // Verify all losing bids are returned in the correct order.
  EXPECT_THAT(
      response.losing_bids(),
      ElementsAre(
          AllOf(Property(&Scored::interest_group_owner, "dsp.example"),
                Property(&Scored::interest_group_name, "ufoconspiracies"),
                Property(&Scored::render_url,
                         "https://dsp.example/ufoconspiracies"),
                Property(&Scored::bid_price, 70.0),
                Property(&Scored::desirability_score, 70.0)),
          AllOf(
              Property(&Scored::interest_group_owner, "dsp.example"),
              Property(&Scored::interest_group_name, "boringreads"),
              Property(&Scored::render_url, "https://dsp.example/boringreads"),
              Property(&Scored::bid_price, 60.0),
              Property(&Scored::desirability_score, 60.0))));
}

TEST_F(AdAuctionsTest, RunAdAuctionWithRefresh) {
  std::unique_ptr<AdAuctions::Service> ad_auctions =
      AdAuctionsImpl::Create(function_source_,
                             WriteStandardAuctionConfiguration(),
                             refresh_periodic_functions_.Factory())
          .value();
  auto request = ParseTextOrDie<RunAdAuctionRequest>(
      R"pb(
        interest_groups {
          owner: "adnetwork.example"
          name: "funnytoons"
          bidding_logic_url: "https://adnetwork.example/bidding/double.js"
          ads {
            render_url: "https://adnetwork.example/funny"
            ad_metadata {
              fields {
                key: "funny"
                value { bool_value: true }
              }
            }
          }
        }
        interest_groups {
          owner: "dsp.example"
          name: "ufoconspiracies"
          bidding_logic_url: "https://dsp.example/bidding/multiply.js"
          ads { render_url: "https://dsp.example/ufoconspiracies" }
          user_bidding_signals {
            fields {
              key: "engagement"
              value { number_value: 3.5 }
            }
          }
        }
        auction_configuration {
          decision_logic_url: "https://ssp.example/auction/preferFunnyAds.js"
          interest_group_buyers: [ "dsp.example", "adnetwork.example" ]
          per_buyer_signals {
            key: "adnetwork.example"
            value {
              fields {
                key: "foo"
                value { number_value: 21 }
              }
            }
          }
          per_buyer_signals {
            key: "dsp.example"
            value {
              fields {
                key: "foo"
                value { number_value: 20 }
              }
            }
          }
        }
      )pb");
  ::aviary::RunAdAuctionResponse response;
  grpc::Status status =
      ad_auctions->RunAdAuction(/*context=*/nullptr, &request, &response);
  ASSERT_TRUE(status.ok());
  EXPECT_TRUE(response.has_winning_bid());
  EXPECT_THAT(
      response.winning_bid(),
      AllOf(Property(&Scored::render_url, "https://adnetwork.example/funny"),
            Property(&Scored::bid_price, 42.0),
            Property(&Scored::desirability_score, 84.0)));

  EXPECT_THAT(response.losing_bids(),
              ElementsAre(AllOf(Property(&Scored::render_url,
                                         "https://dsp.example/ufoconspiracies"),
                                Property(&Scored::bid_price, 70.0),
                                Property(&Scored::desirability_score, 70.0))));
  function_source_.AddRemoteFunction(
      "https://ssp.example/auction/preferFunnyAds.js",
      R"((adMetadata, bid, auctionConfig, trustedScoringSignals, browserSignals) => ({ desirabilityScore: adMetadata && adMetadata.funny ? bid * 1.5 : bid }))");
  response.Clear();
  status = ad_auctions->RunAdAuction(/*context=*/nullptr, &request, &response);
  // Before the refresh, auction outcome should stay unchanged.
  ASSERT_TRUE(status.ok());
  EXPECT_TRUE(response.has_winning_bid());
  EXPECT_THAT(
      response.winning_bid(),
      AllOf(Property(&Scored::render_url, "https://adnetwork.example/funny"),
            Property(&Scored::bid_price, 42.0),
            Property(&Scored::desirability_score, 84.0)));

  refresh_periodic_functions_.InvokeAllNow();
  response.Clear();
  status = ad_auctions->RunAdAuction(/*context=*/nullptr, &request, &response);
  // After the refresh, updated ad scoring function should have been used for
  // ranking.
  ASSERT_TRUE(status.ok());
  EXPECT_TRUE(response.has_winning_bid());
  EXPECT_THAT(response.winning_bid(),
              AllOf(Property(&Scored::render_url,
                             "https://dsp.example/ufoconspiracies"),
                    Property(&Scored::bid_price, 70.0),
                    Property(&Scored::desirability_score, 70.0)));
  EXPECT_THAT(response.losing_bids(),
              ElementsAre(AllOf(Property(&Scored::render_url,
                                         "https://adnetwork.example/funny"),
                                Property(&Scored::bid_price, 42.0),
                                Property(&Scored::desirability_score, 63.0))));

  function_source_.AddRemoteFunction(
      "https://adnetwork.example/bidding/double.js",
      R"((interestGroup, auctionSignals, perBuyerSignals, trustedBiddingSignals, browserSignals) => ({ bid: perBuyerSignals.foo * 3.0,
            renderUrl: interestGroup.ads[0].renderUrl,
            ad: interestGroup.ads[0].adMetadata }))");
  refresh_periodic_functions_.InvokeAllNow();
  response.Clear();
  status = ad_auctions->RunAdAuction(/*context=*/nullptr, &request, &response);
  // After the refresh, updated bidding function should have been used for
  // computing a bid.
  ASSERT_TRUE(status.ok());
  EXPECT_TRUE(response.has_winning_bid());
  EXPECT_THAT(
      response.winning_bid(),
      AllOf(Property(&Scored::render_url, "https://adnetwork.example/funny"),
            Property(&Scored::bid_price, 63.0),
            Property(&Scored::desirability_score, 94.5)));
  EXPECT_THAT(response.losing_bids(),
              ElementsAre(AllOf(Property(&Scored::render_url,
                                         "https://dsp.example/ufoconspiracies"),
                                Property(&Scored::bid_price, 70.0),
                                Property(&Scored::desirability_score, 70.0))));
}

TEST_F(AdAuctionsTest, RunAdAuctionTrustedBiddingSignals) {
  function_source_
      .AddRemoteFunction("https://ssp.example/auction/standardScoring.js",
                         R"(
(adMetadata, bid, auctionConfig, trustedScoringSignals, browserSignals) => ({ desirabilityScore: bid }))")
      .AddRemoteFunction("https://dsp.example/bidding/multiply.js",
                         R"(
(interestGroup, auctionSignals, perBuyerSignals, trustedBiddingSignals, browserSignals) => ({ bid: perBuyerSignals.foo * trustedBiddingSignals.ctr,
            renderUrl: interestGroup.ads[0].renderUrl,
            ad: interestGroup.ads[0].adMetadata }))");
  std::string configuration_filename = WriteYamlConfiguration(R"(
biddingFunctions:
  - uri: https://dsp.example/bidding/multiply.js
adScoringFunctions:
  - uri: https://ssp.example/auction/standardScoring.js
)");
  std::unique_ptr<AdAuctions::Service> ad_auctions =
      AdAuctionsImpl::Create(function_source_, configuration_filename).value();
  auto request = ParseTextOrDie<RunAdAuctionRequest>(
      R"pb(
        interest_groups {
          owner: "dsp.example"
          name: "boringreads"
          bidding_logic_url: "https://dsp.example/bidding/multiply.js"
          ads { render_url: "https://dsp.example/boringreads" }
          trusted_bidding_signals {
            key: "ctr"
            value { number_value: 2.5 }
          }
        }
        interest_groups {
          owner: "dsp.example"
          name: "ufoconspiracies"
          bidding_logic_url: "https://dsp.example/bidding/multiply.js"
          ads { render_url: "https://dsp.example/ufoconspiracies" }
          trusted_bidding_signals {
            key: "ctr"
            value { number_value: 3.5 }
          }
        }
        auction_configuration {
          decision_logic_url: "https://ssp.example/auction/standardScoring.js"
          interest_group_buyers: [ "dsp.example" ]
          per_buyer_signals {
            key: "dsp.example"
            value {
              fields {
                key: "foo"
                value { number_value: 20 }
              }
            }
          }
        }
      )pb");
  ::aviary::RunAdAuctionResponse response;
  grpc::Status status =
      ad_auctions->RunAdAuction(/*context=*/nullptr, &request, &response);
  ASSERT_TRUE(status.ok());
  EXPECT_TRUE(response.has_winning_bid());
  const auto& winning_bid = response.winning_bid();
  EXPECT_EQ(winning_bid.interest_group_owner(), "dsp.example");
  EXPECT_EQ(winning_bid.interest_group_name(), "ufoconspiracies");
  EXPECT_EQ(winning_bid.render_url(), "https://dsp.example/ufoconspiracies");
  EXPECT_EQ(winning_bid.bid_price(), 70.0);
  EXPECT_EQ(winning_bid.desirability_score(), 70.0);

  // Verify all losing bids are returned in the correct order.
  EXPECT_THAT(
      response.losing_bids(),
      ElementsAre(AllOf(
          Property(&Scored::interest_group_owner, "dsp.example"),
          Property(&Scored::interest_group_name, "boringreads"),
          Property(&Scored::render_url, "https://dsp.example/boringreads"),
          Property(&Scored::bid_price, 50.0),
          Property(&Scored::desirability_score, 50.0))));
}

TEST_F(AdAuctionsTest, RunAdAuctionDisallowedBuyerSkipped) {
  std::unique_ptr<AdAuctions::Service> ad_auctions =
      AdAuctionsImpl::Create(function_source_,
                             WriteStandardAuctionConfiguration())
          .value();
  auto request = ParseTextOrDie<RunAdAuctionRequest>(
      R"pb(
        interest_groups {
          owner: "adnetwork.example"
          name: "funnytoons"
          bidding_logic_url: "https://adnetwork.example/bidding/double.js"
          ads {
            render_url: "https://adnetwork.example/funny"
            ad_metadata {
              fields {
                key: "funny"
                value { bool_value: true }
              }
            }
          }
        }
        interest_groups {
          owner: "dsp.example"
          name: "boringreads"
          bidding_logic_url: "https://dsp.example/bidding/multiply.js"
          ads { render_url: "https://dsp.example/boringreads" }
          user_bidding_signals {
            fields {
              key: "engagement"
              value { number_value: 3 }
            }
          }
        }
        interest_groups {
          owner: "dsp.example"
          name: "ufoconspiracies"
          bidding_logic_url: "https://dsp.example/bidding/multiply.js"
          ads { render_url: "https://dsp.example/ufoconspiracies" }
          user_bidding_signals {
            fields {
              key: "engagement"
              value { number_value: 3.5 }
            }
          }
        }
        auction_configuration {
          decision_logic_url: "https://ssp.example/auction/preferFunnyAds.js"
          interest_group_buyers: [ "dsp.example" ]
          per_buyer_signals {
            key: "adnetwork.example"
            value {
              fields {
                key: "foo"
                value { number_value: 21 }
              }
            }
          }
          per_buyer_signals {
            key: "dsp.example"
            value {
              fields {
                key: "foo"
                value { number_value: 20 }
              }
            }
          }
        }
      )pb");
  ::aviary::RunAdAuctionResponse response;
  grpc::Status status =
      ad_auctions->RunAdAuction(/*context=*/nullptr, &request, &response);
  ASSERT_TRUE(status.ok());
  EXPECT_TRUE(response.has_winning_bid());
  const auto& winning_bid = response.winning_bid();
  EXPECT_EQ(winning_bid.interest_group_name(), "ufoconspiracies");
  EXPECT_EQ(winning_bid.interest_group_owner(), "dsp.example");
  EXPECT_EQ(winning_bid.render_url(), "https://dsp.example/ufoconspiracies");
  EXPECT_EQ(winning_bid.bid_price(), 70.0);
  EXPECT_EQ(winning_bid.desirability_score(), 70.0);

  EXPECT_THAT(response.losing_bids(),
              ElementsAre(

                  AllOf(Property(&Scored::interest_group_owner, "dsp.example"),
                        Property(&Scored::interest_group_name, "boringreads"),
                        Property(&Scored::render_url,
                                 "https://dsp.example/boringreads"),
                        Property(&Scored::bid_price, 60.0),
                        Property(&Scored::desirability_score, 60.0))));
}

TEST_F(AdAuctionsTest, RunAdAuctionAllAdsFiltered) {
  std::unique_ptr<AdAuctions::Service> ad_auctions =
      AdAuctionsImpl::Create(function_source_,
                             WriteStandardAuctionConfiguration())
          .value();
  auto request = ParseTextOrDie<RunAdAuctionRequest>(
      R"pb(
        interest_groups {
          owner: "adnetwork.example"
          name: "funnytoons"
          bidding_logic_url: "https://adnetwork.example/bidding/double.js"
          ads {
            render_url: "https://adnetwork.example/funny"
            ad_metadata {
              fields {
                key: "funny"
                value { bool_value: true }
              }
              fields {
                key: "categories"
                value { list_value { values { string_value: "jokes" } } }
              }
            }
          }
        }
        interest_groups {
          owner: "dsp.example"
          name: "boringreads"
          bidding_logic_url: "https://dsp.example/bidding/multiply.js"
          ads {
            render_url: "https://dsp.example/boringreads"
            ad_metadata {
              fields {
                key: "categories"
                value {
                  list_value {
                    values { string_value: "jokes" }
                    values { string_value: "politics" }
                  }
                }
              }
            }
          }
          user_bidding_signals {
            fields {
              key: "engagement"
              value { number_value: 3 }
            }
          }
        }
        interest_groups {
          owner: "dsp.example"
          name: "ufoconspiracies"
          bidding_logic_url: "https://dsp.example/bidding/multiply.js"
          ads {
            render_url: "https://dsp.example/ufoconspiracies"
            ad_metadata {
              fields {
                key: "categories"
                value {
                  list_value {
                    values { string_value: "jokes" }
                    values { string_value: "science" }
                  }
                }
              }
            }
          }
          user_bidding_signals {
            fields {
              key: "engagement"
              value { number_value: 3.5 }
            }
          }
        }
        auction_configuration {
          decision_logic_url: "https://ssp.example/auction/preferBoringAds.js"
          interest_group_buyers: [ "dsp.example", "adnetwork.example" ]
          per_buyer_signals {
            key: "adnetwork.example"
            value {
              fields {
                key: "foo"
                value { number_value: 21 }
              }
            }
          }
          per_buyer_signals {
            key: "dsp.example"
            value {
              fields {
                key: "foo"
                value { number_value: 20 }
              }
            }
          }
        }
      )pb");
  ::aviary::RunAdAuctionResponse response;
  grpc::Status status =
      ad_auctions->RunAdAuction(/*context=*/nullptr, &request, &response);
  ASSERT_TRUE(status.ok());
  EXPECT_FALSE(response.has_winning_bid());

  // Verify all bids are returned as losing.
  EXPECT_THAT(
      response.losing_bids(),
      UnorderedElementsAre(
          AllOf(
              Property(&Scored::interest_group_owner, "adnetwork.example"),
              Property(&Scored::interest_group_name, "funnytoons"),
              Property(&Scored::render_url, "https://adnetwork.example/funny"),
              Property(&Scored::bid_price, 42.0),
              Property(&Scored::desirability_score, 0.0)),
          AllOf(Property(&Scored::interest_group_owner, "dsp.example"),
                Property(&Scored::interest_group_name, "ufoconspiracies"),
                Property(&Scored::render_url,
                         "https://dsp.example/ufoconspiracies"),
                Property(&Scored::bid_price, 70.0),
                Property(&Scored::desirability_score, 0.0)),
          AllOf(
              Property(&Scored::interest_group_owner, "dsp.example"),
              Property(&Scored::interest_group_name, "boringreads"),
              Property(&Scored::render_url, "https://dsp.example/boringreads"),
              Property(&Scored::bid_price, 60.0),
              Property(&Scored::desirability_score, 0.0))));
}

TEST_F(AdAuctionsTest, RunAdAuctionTrustedScoringSignals) {
  std::unique_ptr<AdAuctions::Service> ad_auctions =
      AdAuctionsImpl::Create(function_source_,
                             WriteStandardAuctionConfiguration())
          .value();
  auto request = ParseTextOrDie<RunAdAuctionRequest>(
      R"pb(
        interest_groups {
          owner: "adnetwork.example"
          name: "funnytoons"
          bidding_logic_url: "https://adnetwork.example/bidding/double.js"
          ads {
            render_url: "https://adnetwork.example/funny"
            ad_metadata {
              fields {
                key: "funny"
                value { bool_value: true }
              }
              fields {
                key: "categories"
                value { list_value { values { string_value: "jokes" } } }
              }
            }
          }
        }
        interest_groups {
          owner: "dsp.example"
          name: "boringreads"
          bidding_logic_url: "https://dsp.example/bidding/multiply.js"
          ads { render_url: "https://dsp.example/boringreads" }
          user_bidding_signals {
            fields {
              key: "engagement"
              value { number_value: 3 }
            }
          }
        }
        interest_groups {
          owner: "dsp.example"
          name: "ufoconspiracies"
          bidding_logic_url: "https://dsp.example/bidding/multiply.js"
          ads { render_url: "https://dsp.example/ufoconspiracies" }
          user_bidding_signals {
            fields {
              key: "engagement"
              value { number_value: 3.5 }
            }
          }
        }
        auction_configuration {
          decision_logic_url: "https://ssp.example/auction/preferBoringAdsFromTrustedSignals.js"
          interest_group_buyers: [ "dsp.example", "adnetwork.example" ]
          per_buyer_signals {
            key: "adnetwork.example"
            value {
              fields {
                key: "foo"
                value { number_value: 21 }
              }
            }
          }
          per_buyer_signals {
            key: "dsp.example"
            value {
              fields {
                key: "foo"
                value { number_value: 20 }
              }
            }
          }
        }
        trusted_scoring_signals {
          key: "https://adnetwork.example/funny"
          value {
            fields {
              key: "categories"
              value { list_value { values { string_value: "jokes" } } }
            }
          }
        }
        trusted_scoring_signals {
          key: "https://dsp.example/boringreads"
          value {
            fields {
              key: "categories"
              value { list_value { values { string_value: "politics" } } }
            }
          }
        }
        trusted_scoring_signals {
          key: "https://dsp.example/ufoconspiracies"
          value {
            fields {
              key: "categories"
              value { list_value { values { string_value: "sci-fi" } } }
            }
          }
        }
      )pb");
  ::aviary::RunAdAuctionResponse response;
  grpc::Status status =
      ad_auctions->RunAdAuction(/*context=*/nullptr, &request, &response);
  ASSERT_TRUE(status.ok());
  EXPECT_TRUE(response.has_winning_bid());

  const auto& winning_bid = response.winning_bid();
  EXPECT_EQ(winning_bid.interest_group_name(), "ufoconspiracies");
  EXPECT_EQ(winning_bid.interest_group_owner(), "dsp.example");
  EXPECT_EQ(winning_bid.render_url(), "https://dsp.example/ufoconspiracies");
  EXPECT_EQ(winning_bid.bid_price(), 70.0);
  EXPECT_EQ(winning_bid.desirability_score(), 70.0);

  EXPECT_THAT(
      response.losing_bids(),
      ElementsAre(
          AllOf(
              Property(&Scored::interest_group_owner, "dsp.example"),
              Property(&Scored::interest_group_name, "boringreads"),
              Property(&Scored::render_url, "https://dsp.example/boringreads"),
              Property(&Scored::bid_price, 60.0),
              Property(&Scored::desirability_score, 60.0)),
          AllOf(
              Property(&Scored::interest_group_owner, "adnetwork.example"),
              Property(&Scored::interest_group_name, "funnytoons"),
              Property(&Scored::render_url, "https://adnetwork.example/funny"),
              Property(&Scored::bid_price, 42.0),
              // Lost due to trusted bidding signals contents.
              Property(&Scored::desirability_score, 0.0))));
}

TEST_F(AdAuctionsTest, RunAdAuctionFailingBiddingFunctionSkipped) {
  std::unique_ptr<AdAuctions::Service> ad_auctions =
      AdAuctionsImpl::Create(function_source_,
                             WriteStandardAuctionConfiguration())
          .value();
  auto request = ParseTextOrDie<RunAdAuctionRequest>(
      R"pb(
        interest_groups {
          owner: "adnetwork.example"
          name: "funnytoons"
          bidding_logic_url: "https://dsp.example/bidding/failingBiddingFunction.js"
          ads {
            render_url: "https://adnetwork.example/funny"
            ad_metadata {
              fields {
                key: "funny"
                value { bool_value: true }
              }
            }
          }
        }
        interest_groups {
          owner: "dsp.example"
          name: "boringreads"
          bidding_logic_url: "https://adnetwork.example/bidding/triple.js"
          ads { render_url: "https://dsp.example/boringreads" }
        }
        auction_configuration {
          decision_logic_url: "https://ssp.example/auction/preferFunnyAds.js"
          interest_group_buyers: [ "dsp.example", "adnetwork.example" ]
          per_buyer_signals {
            key: "adnetwork.example"
            value {
              fields {
                key: "foo"
                value { number_value: 21 }
              }
            }
          }
          per_buyer_signals {
            key: "dsp.example"
            value {
              fields {
                key: "foo"
                value { number_value: 20 }
              }
            }
          }
        }
      )pb");
  ::aviary::RunAdAuctionResponse response;
  grpc::Status status =
      ad_auctions->RunAdAuction(/*context=*/nullptr, &request, &response);
  ASSERT_TRUE(status.ok());
  const ScoredInterestGroupBid& bid = response.winning_bid();
  EXPECT_EQ(bid.render_url(), "https://dsp.example/boringreads");
  EXPECT_EQ(bid.bid_price(), 60.0);
  EXPECT_EQ(bid.desirability_score(), 60.0);
  EXPECT_THAT(response.losing_bids(), IsEmpty());
}

TEST_F(AdAuctionsTest, RunAdAuctionMissingBiddingFunctionSkipped) {
  std::unique_ptr<AdAuctions::Service> ad_auctions =
      AdAuctionsImpl::Create(function_source_,
                             WriteStandardAuctionConfiguration())
          .value();
  auto request = ParseTextOrDie<RunAdAuctionRequest>(
      R"pb(
        interest_groups {
          owner: "adnetwork.example"
          name: "funnytoons"
          bidding_logic_url: "https://adnetwork.example/bidding/quadruple"
          ads {
            render_url: "https://adnetwork.example/funny"
            ad_metadata {
              fields {
                key: "funny"
                value { bool_value: true }
              }
            }
          }
        }
        interest_groups {
          owner: "dsp.example"
          name: "boringreads"
          bidding_logic_url: "https://adnetwork.example/bidding/triple.js"
          ads { render_url: "https://dsp.example/boringreads" }
        }
        auction_configuration {
          decision_logic_url: "https://ssp.example/auction/preferFunnyAds.js"
          interest_group_buyers: [ "dsp.example", "adnetwork.example" ]
          per_buyer_signals {
            key: "adnetwork.example"
            value {
              fields {
                key: "foo"
                value { number_value: 21 }
              }
            }
          }
          per_buyer_signals {
            key: "dsp.example"
            value {
              fields {
                key: "foo"
                value { number_value: 20 }
              }
            }
          }
        }
      )pb");
  ::aviary::RunAdAuctionResponse response;
  grpc::Status status =
      ad_auctions->RunAdAuction(/*context=*/nullptr, &request, &response);
  ASSERT_TRUE(status.ok());
  const ScoredInterestGroupBid& bid = response.winning_bid();
  EXPECT_EQ(bid.render_url(), "https://dsp.example/boringreads");
  EXPECT_EQ(bid.bid_price(), 60.0);
  EXPECT_EQ(bid.desirability_score(), 60.0);
  EXPECT_THAT(response.losing_bids(), IsEmpty());
}

TEST_F(AdAuctionsTest, RunAdAuctionFailingScoringFunction) {
  std::unique_ptr<AdAuctions::Service> ad_auctions =
      AdAuctionsImpl::Create(function_source_,
                             WriteStandardAuctionConfiguration())
          .value();
  auto request = ParseTextOrDie<RunAdAuctionRequest>(
      R"pb(
        interest_groups {
          owner: "adnetwork.example"
          name: "funnytoons"
          bidding_logic_url: "https://adnetwork.example/bidding/double.js"
          ads {
            render_url: "https://adnetwork.example/funny"
            ad_metadata {
              fields {
                key: "funny"
                value { bool_value: true }
              }
            }
          }
        }
        auction_configuration {
          decision_logic_url: "https://ssp.example/auction/failingScoringFunction.js"
          interest_group_buyers: [ "dsp.example", "adnetwork.example" ]
          per_buyer_signals {
            key: "adnetwork.example"
            value {
              fields {
                key: "foo"
                value { number_value: 21 }
              }
            }
          }
        }
      )pb");
  ::aviary::RunAdAuctionResponse response;
  grpc::Status status =
      ad_auctions->RunAdAuction(/*context=*/nullptr, &request, &response);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), grpc::StatusCode::INTERNAL);
  EXPECT_FALSE(response.has_winning_bid());
}

TEST_F(AdAuctionsTest, RunAdAuctionMissingScoringFunction) {
  std::unique_ptr<AdAuctions::Service> ad_auctions =
      AdAuctionsImpl::Create(function_source_,
                             WriteStandardAuctionConfiguration())
          .value();
  auto request = ParseTextOrDie<RunAdAuctionRequest>(
      R"pb(
        interest_groups {
          owner: "adnetwork.example"
          name: "funnytoons"
          bidding_logic_url: "https://adnetwork.example/bidding/double.js"
          ads {
            render_url: "https://adnetwork.example/funny"
            ad_metadata {
              fields {
                key: "funny"
                value { bool_value: true }
              }
            }
          }
        }
        auction_configuration {
          decision_logic_url: "preferBeautifulAds"
          interest_group_buyers: [ "dsp.example", "adnetwork.example" ]
          per_buyer_signals {
            key: "adnetwork.example"
            value {
              fields {
                key: "foo"
                value { number_value: 21 }
              }
            }
          }
        }
      )pb");
  ::aviary::RunAdAuctionResponse response;
  grpc::Status status =
      ad_auctions->RunAdAuction(/*context=*/nullptr, &request, &response);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), grpc::StatusCode::NOT_FOUND);
  EXPECT_FALSE(response.has_winning_bid());
}

TEST_F(AdAuctionsTest, CreateFromConfigurationFileRemoteFunctionSpec) {
  function_source_
      .AddRemoteFunction("https://dsp.example/bidding/double.js",
                         "(interestGroup, auctionSignals, perBuyerSignals, "
                         "trustedBiddingSignals, browserSignals) => ({ bid: "
                         "perBuyerSignals.foo * 2 })")
      .AddRemoteFunction(
          "https://dsp.example/bidding/triple.js",
          "(function(interestGroup, auctionSignals, perBuyerSignals, "
          "trustedBiddingSignals, browserSignals) { return { bid: "
          "perBuyerSignals.foo * 3 }; })");
  std::string configuration_filename = WriteYamlConfiguration(R"(
biddingFunctions:
  - uri: https://dsp.example/bidding/double.js
  - uri: https://dsp.example/bidding/triple.js
adScoringFunctions: []
)");
  std::unique_ptr<AdAuctions::Service> ad_auctions =
      AdAuctionsImpl::Create(function_source_, configuration_filename).value();
  auto request = ParseTextOrDie<ComputeBidRequest>(
      R"pb(
        bidding_function_name: "https://dsp.example/bidding/double.js"
        input {
          per_buyer_signals {
            fields {
              key: "foo"
              value { number_value: 21 }
            }
          }
        }
      )pb");
  ::aviary::BiddingFunctionOutput response;
  grpc::Status status =
      ad_auctions->ComputeBid(/*context=*/nullptr, &request, &response);
  ASSERT_TRUE(status.ok());
  EXPECT_EQ(response.bid(), 42.0);

  // Verify that the call gets dispatched to the correct bidding function.
  request.set_bidding_function_name("https://dsp.example/bidding/triple.js");
  status = ad_auctions->ComputeBid(/*context=*/nullptr, &request, &response);
  ASSERT_TRUE(status.ok());
  EXPECT_EQ(response.bid(), 63.0);
}

TEST_F(AdAuctionsTest, CreateFromConfigurationFileDuplicateSpec) {
  function_source_
      .AddRemoteFunction("https://dsp.example/bidding/duplicate.js",
                         "input => input.perBuyerSignals.foo * 2")
      .AddRemoteFunction("https://dsp.example/bidding/triple.js",
                         kTriplingBiddingFunction);
  std::string configuration_filename = WriteYamlConfiguration(R"(
biddingFunctions:
  - name: duplicate
    uri: https://dsp.example/bidding/duplicate.js
  - name: triple
    uri: https://dsp.example/bidding/triple.js
  - name: duplicate
    uri: https://dsp.example/bidding/duplicate.js
adScoringFunctions: []
)");
  auto status =
      AdAuctionsImpl::Create(function_source_, configuration_filename).status();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(status.message()),
              HasSubstr("defined more than once"));
}

TEST_F(AdAuctionsTest, MissingConfigurationFile) {
  auto status = AdAuctionsImpl::Create(
                    function_source_, testing::TempDir() + "/non-existing.yaml")
                    .status();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kNotFound);
  EXPECT_THAT(std::string(status.message()),
              AllOf(HasSubstr("Could not open the YAML configuration file")));
}

TEST_F(AdAuctionsTest, BadConfigurationFile) {
  // Source code must be a string.
  std::string configuration_filename = WriteYamlConfiguration(R"(
biddingFunctions:
  - name: fun
    source: [1, 2, 3]
adScoringFunctions: []
)");

  auto status =
      AdAuctionsImpl::Create(function_source_, configuration_filename).status();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(status.message()),
              AllOf(HasSubstr("Malformed YAML configuration")));

  configuration_filename = WriteYamlConfiguration(R"(
biddingFunctions:
  - name: fun
    source:
      - foo: bar
adScoringFunctions: []
)");

  status =
      AdAuctionsImpl::Create(function_source_, configuration_filename).status();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(status.message()),
              AllOf(HasSubstr("Malformed YAML configuration")));

  // Name must be a string.
  configuration_filename = WriteYamlConfiguration(R"(
biddingFunctions:
  - name: [foo, bar]
    source: "inputs => 42.0;"
adScoringFunctions: []
)");

  status =
      AdAuctionsImpl::Create(function_source_, configuration_filename).status();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(status.message()),
              AllOf(HasSubstr("Malformed YAML configuration")));

  // URI must be a string.
  configuration_filename = WriteYamlConfiguration(R"(
biddingFunctions:
  - name: remoteFunction
    uri: [foo, bar]
adScoringFunctions: []
)");

  status =
      AdAuctionsImpl::Create(function_source_, configuration_filename).status();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(status.message()),
              AllOf(HasSubstr("Malformed YAML configuration")));

  // biddingFunctions must be a map.
  configuration_filename = WriteYamlConfiguration(R"(
biddingFunctions: abc
adScoringFunctions: []
)");

  status =
      AdAuctionsImpl::Create(function_source_, configuration_filename).status();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(status.message()),
              AllOf(HasSubstr("Malformed YAML configuration")));

  configuration_filename = WriteYamlConfiguration(R"(
biddingFunctions
adScoringFunctions: []
)");

  status =
      AdAuctionsImpl::Create(function_source_, configuration_filename).status();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      std::string(status.message()),
      AllOf(HasSubstr("Parsing failure reading the YAML configuration file")));

  // Unparseable YAML.
  configuration_filename = WriteYamlConfiguration(R"([ foo
  bar: invalid,)");

  status =
      AdAuctionsImpl::Create(function_source_, configuration_filename).status();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      std::string(status.message()),
      AllOf(HasSubstr("Parsing failure reading the YAML configuration file")));
}

TEST_F(AdAuctionsTest, ConfigurationFileFunctionDoesNotCompile) {
  std::string configuration_filename = WriteYamlConfiguration(R"(
biddingFunctions:
  - uri: local://double
    source: |
      input => input.perBuyerSignals.
adScoringFunctions: []
)");

  auto auctions_or_status =
      AdAuctionsImpl::Create(function_source_, configuration_filename);
  EXPECT_TRUE(auctions_or_status.ok());
  auto auctions = std::move(auctions_or_status.value());
  auto request = ParseTextOrDie<ComputeBidRequest>(
      R"pb(
        bidding_function_name: "local://double"
        input {
          per_buyer_signals {
            fields {
              key: "foo"
              value { number_value: 21 }
            }
          }
        }
      )pb");
  ::aviary::BiddingFunctionOutput response;
  grpc::Status status =
      auctions->ComputeBid(/*context=*/nullptr, &request, &response);
  EXPECT_FALSE(status.ok());
  // Invoking a bidding function that was configured but didn't compile should
  // result in UNAVAILABLE status.
  EXPECT_EQ(status.error_code(), grpc::StatusCode::UNAVAILABLE);
}

TEST_F(AdAuctionsTest, BiddingFunctionNotFound) {
  auto ad_auctions = CreateAdAuctions(Configuration{
      .bidding_function_specs = {FunctionSpecification{
          .uri = "local://one",
          .source_code = "input => input.perBuyerSignals.foo * 2"}}});
  auto request = ParseTextOrDie<ComputeBidRequest>(
      R"pb(
        bidding_function_name: "local://two"
        input {
          per_buyer_signals {
            fields {
              key: "foo"
              value { number_value: 21 }
            }
          }
        }
      )pb");
  ::aviary::BiddingFunctionOutput response;
  grpc::Status status =
      ad_auctions->ComputeBid(/*context=*/nullptr, &request, &response);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), grpc::StatusCode::NOT_FOUND);
}

TEST_F(AdAuctionsTest, BiddingFunctionInvocationError) {
  auto ad_auctions = CreateAdAuctions(Configuration{
      .bidding_function_specs = {FunctionSpecification{
          .uri = "local://one",
          .source_code = "input => input.perBuyerSignals.foo * 2"}}});
  // Input is missing perBuyerSignals, resulting in an invocation error.
  auto request = ParseTextOrDie<ComputeBidRequest>(
      R"pb(
        bidding_function_name: "local://one"
        input {}
      )pb");
  ::aviary::BiddingFunctionOutput response;
  grpc::Status status =
      ad_auctions->ComputeBid(/*context=*/nullptr, &request, &response);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), grpc::StatusCode::INTERNAL);
}

TEST_F(AdAuctionsTest, AlternateJSFunctionSyntax) {
  function_source_
      .AddRemoteFunction("https://dsp.example/bidding/triple.js", R"(
        function generateBid(interestGroup, auctionSignals,
            perBuyerSignals, trustedBiddingSignals, browserSignals) {
          return { bid: perBuyerSignals.foo * 3 }; })")
      .AddRemoteFunction("https://ssp.example/auction/standardScoring.js", R"(
        function scoreAd(adMetadata, bid, auctionConfig,
                         trustedScoringSignals, browserSignals) {
          return { desirabilityScore: bid }; })");

  std::string configuration_filename = WriteYamlConfiguration(R"(
biddingFunctions:
  - uri: https://dsp.example/bidding/triple.js
adScoringFunctions:
  - uri: https://ssp.example/auction/standardScoring.js
)");
  std::unique_ptr<AdAuctions::Service> ad_auctions =
      AdAuctionsImpl::Create(function_source_, configuration_filename).value();
  auto request = ParseTextOrDie<RunAdAuctionRequest>(
      R"pb(
        interest_groups {
          owner: "dsp.example"
          name: "boringreads"
          bidding_logic_url: "https://dsp.example/bidding/triple.js"
          ads { render_url: "https://dsp.example/boringreads" }
        }
        auction_configuration {
          decision_logic_url: "https://ssp.example/auction/standardScoring.js"
          interest_group_buyers: [ "dsp.example" ]
          per_buyer_signals {
            key: "dsp.example"
            value {
              fields {
                key: "foo"
                value { number_value: 20 }
              }
            }
          }
        }
      )pb");
  ::aviary::RunAdAuctionResponse response;
  grpc::Status status =
      ad_auctions->RunAdAuction(/*context=*/nullptr, &request, &response);
  ASSERT_TRUE(status.ok());
  const ScoredInterestGroupBid& bid = response.winning_bid();
  EXPECT_EQ(bid.bid_price(), 60.0);
  EXPECT_EQ(bid.desirability_score(), 60.0);
}
}  // namespace server
}  // namespace aviary
