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

#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/functional/bind_front.h"
#include "absl/strings/substitute.h"
#include "function/bidding_function.h"
#include "function/sapi_bidding_function.h"
#include "include/yaml-cpp/yaml.h"
#include "server/function_source.h"

ABSL_FLAG(bool,
          use_sandbox2,
          true,
          "Whether to use Sandbox2 for isolating JavaScript functions.");
ABSL_FLAG(absl::Duration,
          function_refresh_interval,
          absl::Minutes(1),
          "Refresh interval for bidding functions and ad scoring functions.");

namespace YAML {
template <>
struct convert<::aviary::server::FunctionSpecification> {
  static bool decode(const YAML::Node& node,
                     ::aviary::server::FunctionSpecification& decoded) {
    if (!node.IsMap()) {
      return false;
    }
    const auto source_node = node["source"];
    if (source_node.IsDefined()) {
      decoded.source_code = source_node.as<std::string>();
    }
    const auto uri_node = node["uri"];
    if (uri_node.IsDefined()) {
      decoded.uri = uri_node.as<std::string>();
    }
    return !decoded.uri.empty();
  }
};
}  // namespace YAML

namespace aviary {
namespace server {

namespace {
using ::aviary::function::BiddingFunctionInterface;
using ::aviary::function::FledgeAdScoringFunction;
using ::aviary::function::FledgeBiddingFunction;
using ::aviary::function::FledgeSapiAdScoringFunction;
using ::aviary::function::FledgeSapiBiddingFunction;
using ::aviary::function::FunctionOptions;

BiddingFunctionInput CreateBiddingFunctionInput(
    const InterestGroupAuctionState& interest_group_state,
    const AuctionConfiguration& auction_configuration) {
  BiddingFunctionInput input;
  const auto& per_buyer_signals_it =
      auction_configuration.per_buyer_signals().find(
          interest_group_state.owner());
  if (per_buyer_signals_it !=
      auction_configuration.per_buyer_signals().cend()) {
    input.mutable_per_buyer_signals()->CopyFrom(per_buyer_signals_it->second);
  }
  input.mutable_auction_signals()->CopyFrom(
      auction_configuration.auction_signals());

  InterestGroup* inputs_interest_group = input.mutable_interest_group();
  inputs_interest_group->set_name(interest_group_state.name());
  inputs_interest_group->set_owner(interest_group_state.owner());
  inputs_interest_group->set_bidding_logic_url(
      interest_group_state.bidding_logic_url());
  for (const auto& ad : interest_group_state.ads()) {
    inputs_interest_group->mutable_ads()->Add()->CopyFrom(ad);
  }

  inputs_interest_group->mutable_user_bidding_signals()->CopyFrom(
      interest_group_state.user_bidding_signals());
  input.mutable_browser_signals()->CopyFrom(
      interest_group_state.browser_signals());
  *input.mutable_trusted_bidding_signals() =
      interest_group_state.trusted_bidding_signals();
  return input;
}

AdScoringFunctionInput CreateAdScoringInputs(
    const BiddingFunctionOutput& output,
    const AuctionConfiguration& auction_configuration,
    const google::protobuf::Map<std::string, google::protobuf::Struct>&
        trusted_scoring_signals) {
  AdScoringFunctionInput ad_scoring_function_input;
  ad_scoring_function_input.mutable_auction_config()->CopyFrom(
      auction_configuration);
  ad_scoring_function_input.mutable_ad_metadata()->CopyFrom(output.ad());
  ad_scoring_function_input.set_bid(output.bid());
  const auto& it = trusted_scoring_signals.find(output.render_url());
  if (it != trusted_scoring_signals.cend()) {
    ad_scoring_function_input.mutable_trusted_scoring_signals()->CopyFrom(
        it->second);
  }
  // TODO(b/191545684): provide browser signals to the ad scoring function
  return ad_scoring_function_input;
}

ScoredInterestGroupBid GetScoredInterestGroupBid(
    const InterestGroupAuctionState& interest_group,
    const BiddingFunctionOutput& bid,
    const AdScoringFunctionOutput& scored_ad) {
  ScoredInterestGroupBid scored_interest_group_bid;
  scored_interest_group_bid.set_interest_group_owner(interest_group.owner());
  scored_interest_group_bid.set_interest_group_name(interest_group.name());
  scored_interest_group_bid.set_bid_price(bid.bid());
  scored_interest_group_bid.set_render_url(bid.render_url());
  scored_interest_group_bid.set_desirability_score(
      scored_ad.desirability_score());
  return scored_interest_group_bid;
}

absl::StatusOr<std::map<std::string, std::string>> GetFunctionSourceCodes(
    const FunctionSource& function_source,
    const std::vector<FunctionSpecification>& specifications) {
  std::map<std::string, std::string> functions_code;
  for (const auto& function_specification : specifications) {
    ASSIGN_OR_RETURN(auto source_code,
                     function_source.GetFunctionCode(function_specification));
    if (!functions_code.insert({function_specification.uri, source_code})
             .second) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Function '$0' defined more than once in the configuration file.",
          function_specification.uri));
    }
  }
  return functions_code;
}

absl::StatusOr<std::unique_ptr<FunctionRepository>> CreateFunctionRepository(
    const Configuration& configuration,
    const FunctionSource& function_source) {
  ASSIGN_OR_RETURN(auto bidding_function_source_codes,
                   GetFunctionSourceCodes(
                       function_source, configuration.bidding_function_specs));
  ASSIGN_OR_RETURN(
      auto ad_scoring_function_source_codes,
      GetFunctionSourceCodes(function_source,
                             configuration.ad_scoring_function_specs));
  absl::flat_hash_map<std::string,
                      std::unique_ptr<BiddingFunctionInterface<
                          BiddingFunctionInput, BiddingFunctionOutput>>>
      bidding_functions;
  bidding_functions.reserve(bidding_function_source_codes.size());
  for (const auto& [uri, source] : bidding_function_source_codes) {
    auto bidding_function_or_status =
        absl::GetFlag(FLAGS_use_sandbox2)
            ? FledgeSapiBiddingFunction::Create(
                  source, FunctionOptions{.flatten_function_arguments = true})
            : FledgeBiddingFunction::Create(
                  source, FunctionOptions{.flatten_function_arguments = true});
    if (bidding_function_or_status.ok()) {
      bidding_functions.insert(
          {uri, std::move(bidding_function_or_status.value())});
    } else {
      // Insert a placeholder to distinguish between unknown bidding functions
      // versus those that are not currently available.
      // TODO: increment a counter or otherwise track an error
      bidding_functions.insert({uri, nullptr});
    }
  }
  absl::flat_hash_map<std::string,
                      std::unique_ptr<BiddingFunctionInterface<
                          AdScoringFunctionInput, AdScoringFunctionOutput>>>
      ad_scoring_functions;
  ad_scoring_functions.reserve(ad_scoring_function_source_codes.size());
  for (const auto& [uri, source] : ad_scoring_function_source_codes) {
    auto ad_scoring_function_or_status =
        absl::GetFlag(FLAGS_use_sandbox2)
            ? FledgeSapiAdScoringFunction::Create(
                  source, FunctionOptions{.flatten_function_arguments = true})
            : FledgeAdScoringFunction::Create(
                  source, FunctionOptions{.flatten_function_arguments = true});
    if (ad_scoring_function_or_status.ok()) {
      ad_scoring_functions.insert(
          {uri, std::move(ad_scoring_function_or_status.value())});
    } else {
      // Insert a placeholder to distinguish between unknown ad scoring
      // functions versus those that are not currently available.
      // TODO: increment a counter or otherwise track an error
      ad_scoring_functions.insert({uri, nullptr});
    }
  }
  return std::make_unique<FunctionRepository>(std::move(bidding_functions),
                                              std::move(ad_scoring_functions));
}
}  // namespace

absl::StatusOr<std::unique_ptr<::aviary::AdAuctions::Service>>
AdAuctionsImpl::Create(
    const FunctionSource& function_source,
    absl::string_view configuration_file_name,
    const ::aviary::util::PeriodicFunctionFactory& periodic_function_factory) {
  try {
    YAML::Node config = YAML::LoadFile(std::string(configuration_file_name));
    Configuration configuration{
        .bidding_function_specs =
            config["biddingFunctions"].as<std::vector<FunctionSpecification>>(),
        .ad_scoring_function_specs =
            config["adScoringFunctions"]
                .as<std::vector<FunctionSpecification>>()};
    return AdAuctionsImpl::Create(configuration, function_source,
                                  periodic_function_factory);
  } catch (YAML::BadFile&) {
    return absl::NotFoundError("Could not open the YAML configuration file");
  } catch (YAML::ParserException&) {
    return absl::InvalidArgumentError(
        "Parsing failure reading the YAML configuration file");
  } catch (YAML::RepresentationException&) {
    return absl::InvalidArgumentError("Malformed YAML configuration");
  }
}

grpc::Status AdAuctionsImpl::ComputeBid(
    ::grpc::ServerContext* context,
    const ::aviary::ComputeBidRequest* request,
    ::aviary::BiddingFunctionOutput* response) {
  const absl::StatusOr<BiddingFunctionOutput> bidding_result =
      RunGenerateBidFunction(request->bidding_function_name(),
                             request->input());
  if (bidding_result.ok()) {
    response->CopyFrom(bidding_result.value());
  } else {
    // TODO(b/194695646): record and expose failure metrics
    return grpc::Status(
        static_cast<grpc::StatusCode>(bidding_result.status().code()),
        std::string(bidding_result.status().message()));
  }
  return grpc::Status::OK;
}

grpc::Status AdAuctionsImpl::RunAdAuction(
    ::grpc::ServerContext* context,
    const ::aviary::RunAdAuctionRequest* request,
    ::aviary::RunAdAuctionResponse* response) {
  absl::flat_hash_set<std::string> interest_group_buyers(
      request->auction_configuration().interest_group_buyers().cbegin(),
      request->auction_configuration().interest_group_buyers().cend());
  std::vector<ScoredInterestGroupBid> scored_bids;
  for (const auto& interest_group : request->interest_groups()) {
    if (!interest_group_buyers.contains(interest_group.owner())) {
      // Skip disallowed interest group owners.
      // Browser clients can perform this pre-filtering before calling
      // RunAdAuctions, but it never hurts to double-check.
      continue;
    }
    const absl::StatusOr<BiddingFunctionOutput> bidding_result =
        RunGenerateBidFunction(
            interest_group.bidding_logic_url(),
            CreateBiddingFunctionInput(interest_group,
                                       request->auction_configuration()));
    if (!bidding_result.ok()) {
      // TODO(b/194695646): record and expose failure metrics
      continue;
    }
    const auto ad_scoring_result = RunScoreAdFunction(
        request->auction_configuration().decision_logic_url(),
        CreateAdScoringInputs(bidding_result.value(),
                              request->auction_configuration(),
                              request->trusted_scoring_signals()));
    if (!ad_scoring_result.ok()) {
      // TODO(b/194695646): record and expose failure metrics
      return grpc::Status(
          static_cast<grpc::StatusCode>(ad_scoring_result.status().code()),
          std::string(ad_scoring_result.status().message()));
    }
    scored_bids.push_back(GetScoredInterestGroupBid(
        interest_group, bidding_result.value(), ad_scoring_result.value()));
  }
  absl::c_sort(scored_bids, [](const auto& first_bid, const auto& second_bid) {
    return first_bid.desirability_score() > second_bid.desirability_score();
  });
  auto loser_it = scored_bids.cbegin();
  if (!scored_bids.empty() && scored_bids.front().desirability_score() > 0) {
    response->mutable_winning_bid()->CopyFrom(scored_bids.front());
    loser_it++;
  }
  response->mutable_losing_bids()->Reserve(scored_bids.end() - loser_it);
  for (; loser_it != scored_bids.end(); ++loser_it) {
    response->mutable_losing_bids()->Add()->CopyFrom(*loser_it);
  }
  return grpc::Status::OK;
}

absl::StatusOr<BiddingFunctionOutput> AdAuctionsImpl::RunGenerateBidFunction(
    absl::string_view bidding_logic_url,
    const BiddingFunctionInput& input) {
  absl::ReaderMutexLock lock(&function_repository_mutex_);
  ASSIGN_OR_RETURN(auto function,
                   function_repository_->GetBiddingFunction(bidding_logic_url));
  ASSIGN_OR_RETURN(auto bids, function->BatchInvoke({input}));
  return bids.back();
}

absl::StatusOr<AdScoringFunctionOutput> AdAuctionsImpl::RunScoreAdFunction(
    absl::string_view ad_scoring_logic_url,
    const AdScoringFunctionInput& input) {
  absl::ReaderMutexLock lock(&function_repository_mutex_);
  ASSIGN_OR_RETURN(auto function, function_repository_->GetAdScoringFunction(
                                      ad_scoring_logic_url));
  ASSIGN_OR_RETURN(auto output, function->BatchInvoke({input}));
  return output.back();
}

absl::StatusOr<std::unique_ptr<::aviary::AdAuctions::Service>>
AdAuctionsImpl::Create(
    const Configuration& configuration,
    const FunctionSource& function_source,
    const util::PeriodicFunctionFactory& periodic_function_factory) {
  ASSIGN_OR_RETURN(auto initial_function_repository,
                   CreateFunctionRepository(configuration, function_source));
  return absl::WrapUnique(new AdAuctionsImpl(
      configuration, function_source, std::move(initial_function_repository),
      periodic_function_factory));
}

AdAuctionsImpl::AdAuctionsImpl(
    const Configuration& configuration,
    const FunctionSource& function_source,
    std::unique_ptr<FunctionRepository> initial_function_repository,
    const ::aviary::util::PeriodicFunctionFactory& periodic_function_factory)
    : function_repository_(std::move(initial_function_repository)),
      repository_refresh_(periodic_function_factory(
          // Copy the configuration object for use during refreshes.
          absl::bind_front(&AdAuctionsImpl::RefreshFunctionRepository,
                           this,
                           configuration,
                           std::ref(function_source)),
          absl::GetFlag(FLAGS_function_refresh_interval),
          absl::GetFlag(FLAGS_function_refresh_interval))) {}

void AdAuctionsImpl::RefreshFunctionRepository(
    const Configuration& configuration,
    const FunctionSource& function_source) {
  absl::StatusOr<std::unique_ptr<FunctionRepository>> repository_or_status =
      CreateFunctionRepository(configuration, function_source);
  if (repository_or_status.ok()) {
    absl::WriterMutexLock lock(&function_repository_mutex_);
    function_repository_ = std::move(repository_or_status.value());
  }
}
}  // namespace server
}  // namespace aviary
