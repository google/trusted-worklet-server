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

#ifndef SERVER_FUNCTION_REPOSITORY_H_
#define SERVER_FUNCTION_REPOSITORY_H_

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "function/bidding_function_interface.h"
#include "proto/bidding_function.pb.h"

namespace aviary::server {

// A snapshot of active, compiled bidding and ad scoring functions.
class FunctionRepository {
 public:
  FunctionRepository(
      absl::flat_hash_map<
          std::string,
          std::unique_ptr<::aviary::function::BiddingFunctionInterface<
              BiddingFunctionInput, BiddingFunctionOutput>>>
          bidding_functions,
      absl::flat_hash_map<
          std::string,
          std::unique_ptr<::aviary::function::BiddingFunctionInterface<
              AdScoringFunctionInput, AdScoringFunctionOutput>>>
          ad_scoring_functions);
  virtual ~FunctionRepository() = default;

  absl::StatusOr<const ::aviary::function::BiddingFunctionInterface<
      BiddingFunctionInput, BiddingFunctionOutput>*>
  GetBiddingFunction(absl::string_view bidding_function_uri) const;

  absl::StatusOr<const ::aviary::function::BiddingFunctionInterface<
      AdScoringFunctionInput, AdScoringFunctionOutput>*>
  GetAdScoringFunction(absl::string_view ad_scoring_function_uri) const;

  FunctionRepository(const FunctionRepository&) = delete;
  FunctionRepository& operator=(const FunctionRepository&) = delete;

 private:
  absl::flat_hash_map<
      std::string, std::unique_ptr<::aviary::function::BiddingFunctionInterface<
                       BiddingFunctionInput, BiddingFunctionOutput>>>
      bidding_functions_;

  absl::flat_hash_map<
      std::string, std::unique_ptr<::aviary::function::BiddingFunctionInterface<
                       AdScoringFunctionInput, AdScoringFunctionOutput>>>
      ad_scoring_functions_;
};
}  // namespace aviary::server

#endif  // SERVER_FUNCTION_REPOSITORY_H_
