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

#include "function/bidding_function.h"

#include "absl/flags/flag.h"
#include "absl/strings/substitute.h"
#include "function/sapi_bidding_function.h"
#include "gmock/gmock.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"
#include "proto/bidding_function.pb.h"
#include "util/parse_proto.h"
#include "v8/v8_platform_initializer.h"

namespace aviary {
namespace function {
namespace {

using ::aviary::util::ParseTextOrDie;
using ::aviary::v8::V8PlatformInitializer;
using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::Matches;
using ::testing::Property;

constexpr int kBiddingFunctionWarmUpIterations = 10;

template <typename T>
class BiddingFunctionTest : public testing::Test {
 protected:
  absl::StatusOr<std::vector<BiddingFunctionOutput>> CreateAndInvoke(
      absl::string_view function_source, const BiddingFunctionInput& input) {
    return T::Create(function_source).value()->BatchInvoke({input});
  }

 private:
  V8PlatformInitializer v8_platform_initializer_;
};

using BiddingFunctionTypes =
    ::testing::Types<FledgeBiddingFunction, FledgeSapiBiddingFunction>;

struct BiddingFunctionTestParam {
  // Translates a parameter to a name. Called by TYPED_TEST_SUITE.
  template <typename Param>
  static std::string GetName(int);
};

template <>
std::string BiddingFunctionTestParam::GetName<FledgeBiddingFunction>(int) {
  return "BiddingFunction";
}
template <>
std::string BiddingFunctionTestParam::GetName<FledgeSapiBiddingFunction>(int) {
  return "SandboxedBiddingFunction";
}

TYPED_TEST_SUITE(BiddingFunctionTest, BiddingFunctionTypes,
                 BiddingFunctionTestParam);

MATCHER_P(EqualsProto, proto,
          absl::StrCat("equals proto ", proto.DebugString())) {
  *result_listener << "where the protocol buffer is " << (arg.DebugString());
  return google::protobuf::util::MessageDifferencer::Equals(arg, proto);
}

MATCHER(HasOutputConversionFailure, "") {
  return arg.code() == absl::StatusCode::kFailedPrecondition &&
         Matches(HasSubstr(
             "Unable to convert the bidding function output from JSON"))(
             std::string(arg.message()));
}

TYPED_TEST(BiddingFunctionTest, HappyPath) {
  auto bidding_function_input = ParseTextOrDie<BiddingFunctionInput>(
      R"pb(
        per_buyer_signals: {
          fields: {
            key: "multiplier"
            value: { number_value: 25 }
          }
        }
        interest_group: {
          user_bidding_signals {
            fields: {
              key: "cpm"
              value: { number_value: 3 }
            }
          }
          ads {
            render_url: "https://cdn.example/ad1.html"
            ad_metadata {
              fields {
                key: "cat"
                value: { list_value: { values: { string_value: "IAB19-6" } } }
              }
            }
          }
          ads {
            render_url: "https://cdn.example/ad2.html"
            ad_metadata {
              fields {
                key: "cat"
                value: { list_value: { values: { string_value: "IAB19-19" } } }
              }
            }
          }
        }
      )pb");
  // We compile the bidding function once. This same compiled bidding function
  // will be used to Invoke twice (with different inputs), to ensure that the
  // compiled bidding function can be reused appropriately.
  auto single_bidding_function = TypeParam::Create(
                                     R"(
      input => ({ bid:
                   (input.interestGroup.userBiddingSignals.cpm *
                   input.perBuyerSignals.multiplier),
                  ad: input.interestGroup.ads[0].adMetadata,
                  renderUrl: input.interestGroup.ads[0].renderUrl
                })
       )")
                                     .value();
  // First Invoke.
  const auto result =
      single_bidding_function->BatchInvoke({bidding_function_input});
  ASSERT_TRUE(result.ok()) << result.status().ToString();
  EXPECT_THAT(
      single_bidding_function->BatchInvoke({bidding_function_input}).value(),
      ElementsAre(
          AllOf(Property(&BiddingFunctionOutput::bid, 75.0),
                Property(&BiddingFunctionOutput::render_url,
                         "https://cdn.example/ad1.html"),
                Property(&BiddingFunctionOutput::ad,
                         EqualsProto(bidding_function_input.interest_group()
                                         .ads(0)
                                         .ad_metadata())))));

  auto bidding_function_input_alternate = ParseTextOrDie<BiddingFunctionInput>(
      R"pb(
        per_buyer_signals: {
          fields: {
            key: "multiplier"
            value: { number_value: 25 }
          }
        }
        interest_group: {
          user_bidding_signals {
            fields: {
              key: "cpm"
              value: { number_value: 2 }
            }
          }
          ads {
            render_url: "https://cdn.example/ad2.html"
            ad_metadata {
              fields {
                key: "cat"
                value: { list_value: { values: { string_value: "IAB19-6" } } }
              }
            }
          }
        }
      )pb");
  // Second Invoke, with different input.
  EXPECT_THAT(
      single_bidding_function->BatchInvoke({bidding_function_input_alternate})
          .value(),
      ElementsAre(AllOf(
          Property(&BiddingFunctionOutput::bid, 50.0),
          Property(&BiddingFunctionOutput::render_url,
                   "https://cdn.example/ad2.html"),
          Property(&BiddingFunctionOutput::ad,
                   EqualsProto(bidding_function_input_alternate.interest_group()
                                   .ads(0)
                                   .ad_metadata())))));
}

TYPED_TEST(BiddingFunctionTest, FlattenedArguments) {
  auto bidding_function_input = ParseTextOrDie<BiddingFunctionInput>(
      R"pb(
        per_buyer_signals: {
          fields: {
            key: "multiplier"
            value: { number_value: 25 }
          }
        }
        interest_group: {
          user_bidding_signals {
            fields: {
              key: "cpm"
              value: { number_value: 3 }
            }
          }
          ads {
            render_url: "https://cdn.example/ad1.html"
            ad_metadata {
              fields {
                key: "cat"
                value: { list_value: { values: { string_value: "IAB19-6" } } }
              }
            }
          }
          ads {
            render_url: "https://cdn.example/ad2.html"
            ad_metadata {
              fields {
                key: "cat"
                value: { list_value: { values: { string_value: "IAB19-19" } } }
              }
            }
          }
        }
        browser_signals {
          fields {
            key: "joinCount"
            value: { number_value: 3 }
          }
        }
        trusted_bidding_signals: {
          key: "pacingMultiplier"
          value: { number_value: 0.5 }
        }
      )pb");
  // We compile the bidding function once. This same compiled bidding function
  // will be used to Invoke twice (with different inputs), to ensure that the
  // compiled bidding function can be reused appropriately.
  auto single_bidding_function =
      TypeParam::Create(R"(
      (interestGroup, auctionSignals, perBuyerSignals, trustedBiddingSignals, browserSignals) => ({ bid:
                   (interestGroup.userBiddingSignals.cpm *
                   perBuyerSignals.multiplier * browserSignals.joinCount * trustedBiddingSignals.pacingMultiplier),
                  ad: interestGroup.ads[0].adMetadata,
                  renderUrl: interestGroup.ads[0].renderUrl
                })
       )",
                        FunctionOptions{.flatten_function_arguments = true})
          .value();
  // First Invoke.
  EXPECT_THAT(
      single_bidding_function->BatchInvoke({bidding_function_input}).value(),
      ElementsAre(
          AllOf(Property(&BiddingFunctionOutput::bid, 112.5),
                Property(&BiddingFunctionOutput::render_url,
                         "https://cdn.example/ad1.html"),
                Property(&BiddingFunctionOutput::ad,
                         EqualsProto(bidding_function_input.interest_group()
                                         .ads(0)
                                         .ad_metadata())))));

  auto bidding_function_input_alternate = ParseTextOrDie<BiddingFunctionInput>(
      R"pb(
        per_buyer_signals: {
          fields: {
            key: "multiplier"
            value: { number_value: 25 }
          }
        }
        interest_group: {
          user_bidding_signals {
            fields: {
              key: "cpm"
              value: { number_value: 2 }
            }
          }
          ads {
            render_url: "https://cdn.example/ad2.html"
            ad_metadata {
              fields {
                key: "cat"
                value: { list_value: { values: { string_value: "IAB19-6" } } }
              }
            }
          }
        }
        browser_signals {
          fields {
            key: "joinCount"
            value: { number_value: 2 }
          }
        }
        trusted_bidding_signals: {
          key: "pacingMultiplier"
          value: { number_value: 0.6 }
        }
      )pb");
  // Second Invoke, with different input.
  EXPECT_THAT(
      single_bidding_function->BatchInvoke({bidding_function_input_alternate})
          .value(),
      ElementsAre(AllOf(
          Property(&BiddingFunctionOutput::bid, 60.0),
          Property(&BiddingFunctionOutput::render_url,
                   "https://cdn.example/ad2.html"),
          Property(&BiddingFunctionOutput::ad,
                   EqualsProto(bidding_function_input_alternate.interest_group()
                                   .ads(0)
                                   .ad_metadata())))));
}

TYPED_TEST(BiddingFunctionTest, InvokeWorksWithInterestGroupCorrectly) {
  auto bidding_function_input = ParseTextOrDie<BiddingFunctionInput>(
      R"pb(
        interest_group: { name: "interest_group_name" }
      )pb");
  EXPECT_THAT(this->CreateAndInvoke(R"(
      (function(input) {
         if (input.interestGroup.name == "interest_group_name") {
           return { bid: 2.9 };
         }
         return { bid: 3.9 };
      }))",
                                    bidding_function_input)
                  .value(),
              ElementsAre(Property(&BiddingFunctionOutput::bid, 2.9)));
}

TYPED_TEST(BiddingFunctionTest, FledgeApiGenerateBidFunctionName) {
  auto bidding_function_input = ParseTextOrDie<BiddingFunctionInput>(
      R"pb(
        interest_group: { name: "interest_group_name" }
      )pb");
  EXPECT_THAT(this->CreateAndInvoke(R"(
      function generateBid(input) {
         if (input.interestGroup.name == "interest_group_name") {
           return { bid: 2.9 };
         }
         return { bid: 3.9 };
      })",
                                    bidding_function_input)
                  .value(),
              ElementsAre(Property(&BiddingFunctionOutput::bid, 2.9)));
}

TYPED_TEST(BiddingFunctionTest, InvokeWorksWithPerBuyerSignalsCorrectly) {
  auto bidding_function_input = ParseTextOrDie<BiddingFunctionInput>(
      R"pb(
        per_buyer_signals: {
          fields: {
            key: "multiplier"
            value: { number_value: 2.2 }
          }
        }
      )pb");
  EXPECT_THAT(this->CreateAndInvoke(R"(
      input => ({ bid: input.perBuyerSignals.multiplier })
       )",
                                    bidding_function_input)
                  .value(),
              ElementsAre(Property(&BiddingFunctionOutput::bid, 2.2)));
}

TYPED_TEST(BiddingFunctionTest, InvokeWorksWithBrowserSignalsCorrectly) {
  auto bidding_function_input = ParseTextOrDie<BiddingFunctionInput>(
      R"pb(
        browser_signals {
          fields: {
            key: "top_window_hostname"
            value: { string_value: "shoe.example" }
          }
        }
      )pb");
  EXPECT_THAT(this->CreateAndInvoke(R"(
      input => ({ bid: input.browserSignals.top_window_hostname == "shoe.example" ? 4.2 : 0.0 });
       )",
                                    bidding_function_input)
                  .value(),
              ElementsAre(Property(&BiddingFunctionOutput::bid, 4.2)));
}

TYPED_TEST(BiddingFunctionTest, InvokeWorksWithSimpleAsyncFunction) {
  EXPECT_THAT(
      this->CreateAndInvoke("async i => ({ bid: 1 })", BiddingFunctionInput())
          .value(),
      ElementsAre(Property(&BiddingFunctionOutput::bid, 1.0)));
}

TYPED_TEST(BiddingFunctionTest, InvokeWorksWithAwait) {
  auto bidding_function_input = ParseTextOrDie<BiddingFunctionInput>(
      R"pb(
        per_buyer_signals: {
          fields: {
            key: "multiplier"
            value: { number_value: 2.0 }
          }
        }
      )pb");
  EXPECT_THAT(this->CreateAndInvoke(R"(
    async i => await (async(x) => ({ bid: x * x }))(i.perBuyerSignals.multiplier)
  )",
                                    bidding_function_input)
                  .value(),
              ElementsAre(Property(&BiddingFunctionOutput::bid, 4.0)));
}

TYPED_TEST(BiddingFunctionTest, InvokeWorksWithExplicitPromise) {
  EXPECT_THAT(this->CreateAndInvoke(R"(
    async i => await new Promise(r => r({ bid: 5 }))
  )",
                                    BiddingFunctionInput())
                  .value(),
              ElementsAre(Property(&BiddingFunctionOutput::bid, 5.0)));
}

TYPED_TEST(BiddingFunctionTest, InvokeWorksIfAsyncPartIsSlow) {
  auto bidding_function = TypeParam::Create(R"(
    async i => {
      async function composites(b) {
        var c = new Array(b);
        c[0] = true;
        c[1] = true;
        for (var i = 2; i < b; i++) {
          if (c[i]) { continue; }
          for (var j = i*2; j < b; j+=i) {
            c[j] = true;
          }
        }
        return c;
      }

      return await composites(1000000).then(c => {
        // mod sum the prime numbers
        var s = 0;
        for (var i = 0; i < c.length; i++) {
          if (!c[i]) {
            s = (s + i) % 1000;
          }
        }
        return { bid: s };
      });
    }
  )")
                              .value();
  // We want to test a function that does something slowish, so there is a
  // wait for results. This test function finds the last three digits of the sum
  // of all prime numbers under 1 million.
  EXPECT_THAT(bidding_function->BatchInvoke({BiddingFunctionInput()}).value(),
              ElementsAre(Property(&BiddingFunctionOutput::bid, 23.0)));
}

TYPED_TEST(BiddingFunctionTest, InvokeFailsGracefullyIfAsyncTimeout) {
  auto result = this->CreateAndInvoke(R"(
   async i => {
     return await new Promise(r => { /* (never resolves) */ });
   }
   )",
                                      BiddingFunctionInput());
  // TODO(b/191545684): add convenience status matchers.
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(result.status().message()),
              AllOf(HasSubstr("Async"), HasSubstr("timed out")));
}

TYPED_TEST(BiddingFunctionTest, InvokeFailsGracefullyIfAsyncFailed) {
  auto result = this->CreateAndInvoke(R"(
   async i => thisFunctionDoesNotExist();
   )",
                                      BiddingFunctionInput());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(result.status().message()),
              AllOf(HasSubstr("Async"), HasSubstr("failed"),
                    HasSubstr("ReferenceError"),
                    HasSubstr("thisFunctionDoesNotExist")));
}

TYPED_TEST(BiddingFunctionTest,
           BiddingFunctionProvidesTheWrongOutputValueTypeError) {
  const auto input = ParseTextOrDie<BiddingFunctionInput>(
      R"pb(
        per_buyer_signals: {
          fields: {
            key: "multiplier"
            value: { number_value: 25 }
          }
        }
      )pb");
  EXPECT_THAT(this->CreateAndInvoke(R"(r => "abc")", input).status(),
              HasOutputConversionFailure());
  EXPECT_THAT(
      this->CreateAndInvoke(R"(r => ({bid: "bidvalue"}))", input).status(),
      HasOutputConversionFailure());
  EXPECT_THAT(this->CreateAndInvoke(R"(r => ({bid: {key: "bidvalue"}}))", input)
                  .status(),
              HasOutputConversionFailure());
  EXPECT_THAT(
      this->CreateAndInvoke(R"(r => ({renderUrl: 2.5}))", input).status(),
      HasOutputConversionFailure());
  EXPECT_THAT(
      this->CreateAndInvoke(R"(r => ({renderUrl: { key: "value"}}))", input)
          .status(),
      HasOutputConversionFailure());
  EXPECT_THAT(this->CreateAndInvoke(R"(r => ({ad: "ad"}))", input).status(),
              HasOutputConversionFailure());
}

TYPED_TEST(BiddingFunctionTest, BiddingFunctionSuccessfullyReturnsZero) {
  auto bidding_function_input = ParseTextOrDie<BiddingFunctionInput>(
      R"pb(
        per_buyer_signals: {
          fields: {
            key: "multiplier"
            value: { number_value: 25 }
          }
        }
      )pb");
  EXPECT_THAT(
      this->CreateAndInvoke(R"(r => ({ bid: 0 }))", bidding_function_input)
          .value(),
      ElementsAre(Property(&BiddingFunctionOutput::bid, 0.0)));
}

TYPED_TEST(BiddingFunctionTest, DoesNotReuseContext) {
  auto bidding_function = TypeParam::Create(R"(
    var global_counter = 0;
    (function(input) { return { bid: global_counter++ }; })
  )")
                              .value();
  for (int iteration = 0; iteration < 2; iteration++) {
    EXPECT_THAT(
        bidding_function->BatchInvoke({BiddingFunctionInput()}).value(),
        ElementsAre(
            // One-time warmup during Create() will run the bidding
            // function several times, but afterwards the counter
            // should be the same for all subsequent invocations.
            Property(&BiddingFunctionOutput::bid,
                     static_cast<double>(kBiddingFunctionWarmUpIterations))));
  }
}

TYPED_TEST(BiddingFunctionTest, ExecutionError) {
  auto bidding_function_input = ParseTextOrDie<BiddingFunctionInput>(
      R"pb(
        per_buyer_signals: {
          fields: {
            key: "multiplier"
            value: { number_value: 25 }
          }
        }
      )pb");
  auto result = this->CreateAndInvoke(
      R"((function(bidding_function_input) { return bad;}))",
      bidding_function_input);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInternal);
  EXPECT_THAT(std::string(result.status().message()),
              HasSubstr("Function execution failed"));
}

TYPED_TEST(BiddingFunctionTest, CompilationError) {
  EXPECT_EQ(
      TypeParam::Create(R"((function(args) { garbage... }))").status().code(),
      absl::StatusCode::kInvalidArgument);
}

TYPED_TEST(BiddingFunctionTest, BadScript) {
  auto result = TypeParam::Create(
      R"(
           (function(request) { return 'hey ' + request;});
            bod(); )");
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(std::string(result.status().message()),
              HasSubstr("Cannot run the script"));
}

TYPED_TEST(BiddingFunctionTest, NotAFunction) {
  auto result = TypeParam::Create("'abc';");
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(
      std::string(result.status().message()),
      HasSubstr(
          "Globally-declared object with the expected FLEDGE function name "
          "was not a function."));
}

TYPED_TEST(BiddingFunctionTest, VerifyWarmupSuccess) {
  auto bidding_function = TypeParam::Create(R"((() => {
      var initializedState = null;
      const initialize = function() {
        initializedState = { field: 2 };
      };
      return (function(inputs) {
        var precomputedValue;
        try {
          precomputedValue = initializedState.field;
        } catch (err) {
          precomputedValue = 0;  // zero if not warmed up.
          initialize();
        }
        return { bid: precomputedValue };
      })})())")
                              .value();
  EXPECT_THAT(bidding_function->BatchInvoke({BiddingFunctionInput()}).value(),
              ElementsAre(Property(&BiddingFunctionOutput::bid, 2.0)));
}

TYPED_TEST(BiddingFunctionTest, BatchInvokeSuccess) {
  auto input_one = ParseTextOrDie<BiddingFunctionInput>(
      R"pb(
        per_buyer_signals: {
          fields: {
            key: "multiplier"
            value: { number_value: 3 }
          }
        }
      )pb");
  auto input_two = ParseTextOrDie<BiddingFunctionInput>(
      R"pb(
        per_buyer_signals: {
          fields: {
            key: "multiplier"
            value: { number_value: 5 }
          }
        }
      )pb");
  auto bidding_function = TypeParam::Create(R"(
      (function(input) {
         return { bid: input.perBuyerSignals.multiplier };
      }))")
                              .value();
  EXPECT_THAT(bidding_function->BatchInvoke({input_one, input_two}).value(),
              ElementsAre(Property(&BiddingFunctionOutput::bid, 3.0),
                          Property(&BiddingFunctionOutput::bid, 5.0)));
}

template <typename T>
class AdScoringFunctionTest : public testing::Test {
 protected:
  absl::StatusOr<std::vector<AdScoringFunctionOutput>> CreateAndInvoke(
      absl::string_view function_source, const AdScoringFunctionInput& input) {
    return T::Create(function_source).value()->BatchInvoke({input});
  }

 private:
  V8PlatformInitializer v8_platform_initializer_;
};

using AdScoringFunctionTypes =
    ::testing::Types<FledgeAdScoringFunction, FledgeSapiAdScoringFunction>;

struct AdScoringFunctionTestParam {
  // Translates a parameter to a name. Called by TYPED_TEST_SUITE.
  template <typename Param>
  static std::string GetName(int);
};

template <>
std::string AdScoringFunctionTestParam::GetName<FledgeAdScoringFunction>(int) {
  return "BiddingFunction";
}
template <>
std::string AdScoringFunctionTestParam::GetName<FledgeSapiAdScoringFunction>(
    int) {
  return "SandboxedBiddingFunction";
}

TYPED_TEST_SUITE(AdScoringFunctionTest, AdScoringFunctionTypes,
                 AdScoringFunctionTestParam);

TYPED_TEST(AdScoringFunctionTest, HappyPath) {
  auto ad_scoring_function_input = ParseTextOrDie<AdScoringFunctionInput>(
      R"pb(
        ad_metadata: {
          fields: {
            key: "cat"
            value: { list_value: { values: { string_value: "IAB19-6" } } }
          }
        }
        bid: 1.0
        auction_config: {
          auction_signals: {
            fields: {
              key: "blocked_cat"
              value: { list_value: { values: { string_value: "IAB19-6" } } }
            }
          }
        }
      )pb");
  auto ad_scoring_function = TypeParam::Create(R"(
      (function(input) {
         let blockedCategories =
           input.adMetadata.cat.filter(c => input.auctionConfig.auctionSignals.blocked_cat.includes(c));
         if (blockedCategories.length) {
           // Category filtered.
           return { desirabilityScore: 0 };
         }
         return { desirabilityScore: input.bid * 0.9 };
      })
)")
                                 .value();
  EXPECT_THAT(
      ad_scoring_function->BatchInvoke({ad_scoring_function_input}).value(),
      ElementsAre(
          AllOf(Property(&AdScoringFunctionOutput::desirability_score, 0))));
  auto alternative_ad_scoring_function_input =
      ParseTextOrDie<AdScoringFunctionInput>(
          R"pb(
            ad_metadata: {
              fields: {
                key: "cat"
                value: { list_value: { values: { string_value: "IAB19-1" } } }
              }
            }
            bid: 2.0
            auction_config: {
              auction_signals: {
                fields: {
                  key: "blocked_cat"
                  value: { list_value: { values: { string_value: "IAB19-6" } } }
                }
              }
            }
          )pb");
  EXPECT_THAT(
      ad_scoring_function->BatchInvoke({alternative_ad_scoring_function_input})
          .value(),
      ElementsAre(
          AllOf(Property(&AdScoringFunctionOutput::desirability_score, 1.8))));
}

TYPED_TEST(AdScoringFunctionTest, FlattenedArguments) {
  auto ad_scoring_function_input = ParseTextOrDie<AdScoringFunctionInput>(
      R"pb(
        ad_metadata: {
          fields: {
            key: "cat"
            value: { list_value: { values: { string_value: "IAB19-6" } } }
          }
        }
        bid: 1.0
        auction_config: {
          auction_signals: {
            fields: {
              key: "blockedCat"
              value: { list_value: { values: { string_value: "IAB19-6" } } }
            }
          }
        }
      )pb");
  auto ad_scoring_function =
      TypeParam::Create(R"(
      (function(adMetadata, bid, auctionConfig, trustedScoringSignals, browserSignals) {
         let blockedCategories =
           adMetadata.cat.filter(c => auctionConfig.auctionSignals.blockedCat.includes(c));
         if (blockedCategories.length) {
           // Category filtered.
           return { desirabilityScore: 0 };
         }
         return { desirabilityScore: bid * 0.9 };
      })
)",
                        FunctionOptions{.flatten_function_arguments = true})
          .value();
  auto invocation_result =
      ad_scoring_function->BatchInvoke({ad_scoring_function_input});
  EXPECT_TRUE(invocation_result.ok()) << invocation_result.status().ToString();
  EXPECT_THAT(invocation_result.value(),
              ElementsAre(AllOf(
                  Property(&AdScoringFunctionOutput::desirability_score, 0))));
  auto alternative_ad_scoring_function_input =
      ParseTextOrDie<AdScoringFunctionInput>(
          R"pb(
            ad_metadata: {
              fields: {
                key: "cat"
                value: { list_value: { values: { string_value: "IAB19-1" } } }
              }
            }
            bid: 2.0
            auction_config: {
              auction_signals: {
                fields: {
                  key: "blockedCat"
                  value: { list_value: { values: { string_value: "IAB19-6" } } }
                }
              }
            }
          )pb");
  invocation_result =
      ad_scoring_function->BatchInvoke({alternative_ad_scoring_function_input});
  EXPECT_TRUE(invocation_result.ok()) << invocation_result.status().ToString();
  EXPECT_THAT(invocation_result.value(),
              ElementsAre(AllOf(Property(
                  &AdScoringFunctionOutput::desirability_score, 1.8))));
}

TYPED_TEST(AdScoringFunctionTest, PropagatesTrustedScoringSignals) {
  auto ad_scoring_function_input = ParseTextOrDie<AdScoringFunctionInput>(
      R"pb(
        trusted_scoring_signals: {
          fields: {
            key: "cat"
            value: { list_value: { values: { string_value: "IAB19-6" } } }
          }
        }
        bid: 1.0
        auction_config: {
          auction_signals: {
            fields: {
              key: "blockedCat"
              value: { list_value: { values: { string_value: "IAB19-6" } } }
            }
          }
        }
      )pb");
  auto ad_scoring_function = TypeParam::Create(R"(
      (function(input) {
         let blockedCategories =
           input.trustedScoringSignals.cat.filter(c => input.auctionConfig.auctionSignals.blockedCat.includes(c));
         if (blockedCategories.length) {
           // Category filtered.
           return { desirabilityScore: 0 };
         }
         return { desirabilityScore: input.bid * 0.9 };
      })
)")
                                 .value();
  EXPECT_THAT(
      ad_scoring_function->BatchInvoke({ad_scoring_function_input}).value(),
      ElementsAre(
          AllOf(Property(&AdScoringFunctionOutput::desirability_score, 0))));
  auto alternative_ad_scoring_function_input =
      ParseTextOrDie<AdScoringFunctionInput>(
          R"pb(
            trusted_scoring_signals: {
              fields: {
                key: "cat"
                value: { list_value: { values: { string_value: "IAB19-1" } } }
              }
            }
            bid: 1.0
            auction_config: {
              auction_signals: {
                fields: {
                  key: "blockedCat"
                  value: { list_value: { values: { string_value: "IAB19-6" } } }
                }
              }
            }
          )pb");
  EXPECT_THAT(
      ad_scoring_function->BatchInvoke({alternative_ad_scoring_function_input})
          .value(),
      ElementsAre(
          AllOf(Property(&AdScoringFunctionOutput::desirability_score, 0.9))));
}

TYPED_TEST(AdScoringFunctionTest, PropagatesTrustedScoringSignalsFlattened) {
  auto ad_scoring_function_input = ParseTextOrDie<AdScoringFunctionInput>(
      R"pb(
        trusted_scoring_signals: {
          fields: {
            key: "cat"
            value: { list_value: { values: { string_value: "IAB19-6" } } }
          }
        }
        bid: 1.0
        auction_config: {
          auction_signals: {
            fields: {
              key: "blockedCat"
              value: { list_value: { values: { string_value: "IAB19-6" } } }
            }
          }
        }
      )pb");
  auto ad_scoring_function =
      TypeParam::Create(R"(
      (function(adMetadata, bid, auctionConfig, trustedScoringSignals, browserSignals) {
         let blockedCategories =
           trustedScoringSignals.cat.filter(c => auctionConfig.auctionSignals.blockedCat.includes(c));
         if (blockedCategories.length) {
           // Category filtered.
           return { desirabilityScore: 0 };
         }
         return { desirabilityScore: bid * 0.9 };
      })
)",
                        FunctionOptions{.flatten_function_arguments = true})
          .value();
  auto invocation_result =
      ad_scoring_function->BatchInvoke({ad_scoring_function_input});
  EXPECT_TRUE(invocation_result.ok()) << invocation_result.status().ToString();
  EXPECT_THAT(invocation_result.value(),
              ElementsAre(AllOf(
                  Property(&AdScoringFunctionOutput::desirability_score, 0))));
  auto alternative_ad_scoring_function_input =
      ParseTextOrDie<AdScoringFunctionInput>(
          R"pb(
            trusted_scoring_signals: {
              fields: {
                key: "cat"
                value: { list_value: { values: { string_value: "IAB19-1" } } }
              }
            }
            bid: 1.0
            auction_config: {
              auction_signals: {
                fields: {
                  key: "blockedCat"
                  value: { list_value: { values: { string_value: "IAB19-6" } } }
                }
              }
            }
          )pb");
  invocation_result =
      ad_scoring_function->BatchInvoke({alternative_ad_scoring_function_input});
  EXPECT_TRUE(invocation_result.ok()) << invocation_result.status().ToString();
  EXPECT_THAT(invocation_result.value(),
              ElementsAre(AllOf(Property(
                  &AdScoringFunctionOutput::desirability_score, 0.9))));
}

TYPED_TEST(AdScoringFunctionTest, PropagatesBrowserSignals) {
  auto ad_scoring_function_input = ParseTextOrDie<AdScoringFunctionInput>(
      R"pb(
        browser_signals: {
          fields: {
            key: "interestGroupOwner"
            value: { string_value: "adnetwork.example" }
          }
        }
        bid: 2.0
        auction_config: {
          seller_signals: {
            fields: {
              key: "perBuyerRevshare"
              value: {
                struct_value: {
                  fields: {
                    key: "dsp.example"
                    value: { number_value: 0.1 }
                  }
                  fields: {
                    key: "adnetwork.example"
                    value: { number_value: 0.05 }
                  }
                }
              }
            }
          }
        }
      )pb");
  auto ad_scoring_function = TypeParam::Create(R"(
      (function(input) {
         return { desirabilityScore:
            input.bid *
            (1.0 - input.auctionConfig.sellerSignals.perBuyerRevshare[input.browserSignals.interestGroupOwner]) };
      })
)")
                                 .value();
  EXPECT_THAT(
      ad_scoring_function->BatchInvoke({ad_scoring_function_input}).value(),
      ElementsAre(
          AllOf(Property(&AdScoringFunctionOutput::desirability_score, 1.9))));
}

TYPED_TEST(AdScoringFunctionTest, PropagatesBrowserSignalsFlattened) {
  auto ad_scoring_function_input = ParseTextOrDie<AdScoringFunctionInput>(
      R"pb(
        browser_signals: {
          fields: {
            key: "interestGroupOwner"
            value: { string_value: "adnetwork.example" }
          }
        }
        bid: 2.0
        auction_config: {
          seller_signals: {
            fields: {
              key: "perBuyerRevshare"
              value: {
                struct_value: {
                  fields: {
                    key: "dsp.example"
                    value: { number_value: 0.1 }
                  }
                  fields: {
                    key: "adnetwork.example"
                    value: { number_value: 0.05 }
                  }
                }
              }
            }
          }
        }
      )pb");
  auto ad_scoring_function =
      TypeParam::Create(R"(
      (function(adMetadata, bid, auctionConfig, trustedScoringSignals, browserSignals) {
         return { desirabilityScore:
            bid *
            (1.0 - auctionConfig.sellerSignals.perBuyerRevshare[browserSignals.interestGroupOwner]) };
      })
)",
                        FunctionOptions{.flatten_function_arguments = true})
          .value();
  EXPECT_THAT(
      ad_scoring_function->BatchInvoke({ad_scoring_function_input}).value(),
      ElementsAre(
          AllOf(Property(&AdScoringFunctionOutput::desirability_score, 1.9))));
}

TYPED_TEST(AdScoringFunctionTest, PropagatesInterestGroupBuyers) {
  auto disallowed_ad_scoring_function_input =
      ParseTextOrDie<AdScoringFunctionInput>(
          R"pb(
            browser_signals: {
              fields: {
                key: "interestGroupOwner"
                value: { string_value: "adnetwork.example" }
              }
            }
            bid: 2.0
            auction_config: { interest_group_buyers: [ "dsp.example" ] }
          )pb");
  auto allowed_ad_scoring_function_input =
      ParseTextOrDie<AdScoringFunctionInput>(
          R"pb(
            browser_signals: {
              fields: {
                key: "interestGroupOwner"
                value: { string_value: "dsp.example" }
              }
            }
            bid: 1.5
            auction_config: { interest_group_buyers: [ "dsp.example" ] }
          )pb");
  auto ad_scoring_function = TypeParam::Create(R"(
      (function(input) {
         if (!input.auctionConfig.interestGroupBuyers.includes(input.browserSignals.interestGroupOwner)) {
           return { desirabilityScore: 0 };
         }
         return { desirabilityScore: input.bid };
      })
)")
                                 .value();
  auto invocation_status =
      ad_scoring_function->BatchInvoke({disallowed_ad_scoring_function_input,
                                        allowed_ad_scoring_function_input});
  EXPECT_THAT(
      invocation_status.value(),
      ElementsAre(
          AllOf(Property(&AdScoringFunctionOutput::desirability_score, 0.0)),
          AllOf(Property(&AdScoringFunctionOutput::desirability_score, 1.5))));
}

TYPED_TEST(AdScoringFunctionTest, FledgeApiAdScoringFunctionName) {
  auto allowed_ad_scoring_function_input =
      ParseTextOrDie<AdScoringFunctionInput>(
          R"pb(
            browser_signals: {
              fields: {
                key: "interestGroupOwner"
                value: { string_value: "dsp.example" }
              }
            }
            bid: 1.5
            auction_config: { interest_group_buyers: [ "dsp.example" ] }
          )pb");
  EXPECT_THAT(
      this->CreateAndInvoke(R"(
      function scoreAd(input) {
         if (!input.auctionConfig.interestGroupBuyers.includes(input.browserSignals.interestGroupOwner)) {
           return { desirabilityScore: 0 };
         }
         return { desirabilityScore: input.bid };
      })",
                            allowed_ad_scoring_function_input)
          .value(),
      ElementsAre(Property(&AdScoringFunctionOutput::desirability_score, 1.5)));
}

}  // namespace
}  // namespace function
}  // namespace aviary
