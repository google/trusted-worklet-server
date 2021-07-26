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

#ifndef SERVER_AD_AUCTIONS_H_
#define SERVER_AD_AUCTIONS_H_

#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "function/bidding_function_interface.h"
#include "grpc++/grpc++.h"
#include "proto/aviary.grpc.pb.h"
#include "proto/aviary.pb.h"
#include "server/function_repository.h"
#include "server/function_source.h"
#include "util/periodic_function.h"
#include "util/status_macros.h"

namespace aviary {
namespace server {

// Specifies bidding and ad scoring functions that should be made available for
// execution in the Aviary server. Directly corresponds to the server YAML
// configuration.
struct Configuration {
  std::vector<FunctionSpecification> bidding_function_specs;
  std::vector<FunctionSpecification> ad_scoring_function_specs;
};

// Implements AdAuctions gRPC service.
class AdAuctionsImpl final : public ::aviary::AdAuctions::Service {
 public:
  static absl::StatusOr<std::unique_ptr<::aviary::AdAuctions::Service>> Create(
      const Configuration& configuration, const FunctionSource& function_source,
      const ::aviary::util::PeriodicFunctionFactory& periodic_function_factory =
          ::aviary::util::PeriodicFunction::DefaultFactory());

  static absl::StatusOr<std::unique_ptr<::aviary::AdAuctions::Service>> Create(
      const FunctionSource& function_source,
      absl::string_view configuration_file_name,
      const ::aviary::util::PeriodicFunctionFactory& periodic_function_factory =
          ::aviary::util::PeriodicFunction::DefaultFactory());

  grpc::Status ComputeBid(::grpc::ServerContext* context,
                          const ::aviary::ComputeBidRequest* request,
                          ::aviary::BiddingFunctionOutput* response) override;

  grpc::Status RunAdAuction(::grpc::ServerContext* context,
                            const ::aviary::RunAdAuctionRequest* request,
                            ::aviary::RunAdAuctionResponse* response) override;

 private:
  AdAuctionsImpl(
      const Configuration& configuration, const FunctionSource& function_source,
      std::unique_ptr<FunctionRepository> initial_function_repository,
      const ::aviary::util::PeriodicFunctionFactory& periodic_function_factory);

  void RefreshFunctionRepository(const Configuration& configuration,
                                  const FunctionSource& function_source)
      ABSL_LOCKS_EXCLUDED(function_repository_mutex_);

  absl::StatusOr<BiddingFunctionOutput> RunGenerateBidFunction(
      absl::string_view bidding_logic_url, const BiddingFunctionInput& input)
      ABSL_LOCKS_EXCLUDED(function_repository_mutex_);

  absl::StatusOr<AdScoringFunctionOutput> RunScoreAdFunction(
      absl::string_view ad_scoring_logic_url,
      const AdScoringFunctionInput& input)
      ABSL_LOCKS_EXCLUDED(function_repository_mutex_);

  absl::Mutex function_repository_mutex_;
  std::unique_ptr<FunctionRepository> function_repository_
      ABSL_GUARDED_BY(function_repository_mutex_);
  std::unique_ptr<::aviary::util::PeriodicFunction> repository_refresh_;
};
}  // namespace server
}  // namespace aviary

#endif  // SERVER_AD_AUCTIONS_H_
