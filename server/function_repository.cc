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

#include "absl/strings/substitute.h"

namespace aviary::server {

absl::StatusOr<const ::aviary::function::BiddingFunctionInterface<
    BiddingFunctionInput, BiddingFunctionOutput>*>
FunctionRepository::GetBiddingFunction(
    absl::string_view bidding_function_uri) const {
  const auto function_it = bidding_functions_.find(bidding_function_uri);
  if (function_it == bidding_functions_.end()) {
    return absl::NotFoundError(absl::Substitute("Bidding function $0 not found",
                                                bidding_function_uri));
  }
  if (function_it->second == nullptr) {
    return absl::UnavailableError(absl::Substitute(
        "Bidding function $0 is not available", bidding_function_uri));
  }
  return function_it->second.get();
}

absl::StatusOr<const ::aviary::function::BiddingFunctionInterface<
    AdScoringFunctionInput, AdScoringFunctionOutput>*>
FunctionRepository::GetAdScoringFunction(
    absl::string_view ad_scoring_function_uri) const {
  const auto function_it = ad_scoring_functions_.find(ad_scoring_function_uri);
  if (function_it == ad_scoring_functions_.end()) {
    return absl::NotFoundError(absl::Substitute(
        "Ad scoring function $0 not found", ad_scoring_function_uri));
  }
  if (function_it->second == nullptr) {
    return absl::UnavailableError(absl::Substitute(
        "Ad scoring function $0 is not available", ad_scoring_function_uri));
  }
  return function_it->second.get();
}

FunctionRepository::FunctionRepository(
    absl::flat_hash_map<
        std::string,
        std::unique_ptr<::aviary::function::BiddingFunctionInterface<
            BiddingFunctionInput, BiddingFunctionOutput>>>
        bidding_functions,
    absl::flat_hash_map<
        std::string,
        std::unique_ptr<::aviary::function::BiddingFunctionInterface<
            AdScoringFunctionInput, AdScoringFunctionOutput>>>
        ad_scoring_functions)
    : bidding_functions_(std::move(bidding_functions)),
      ad_scoring_functions_(std::move(ad_scoring_functions)) {}
}  // namespace aviary::server
