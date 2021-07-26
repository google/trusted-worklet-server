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

#include "util/periodic_function.h"

#include "absl/time/clock.h"

namespace aviary::util {
PeriodicFunction::PeriodicFunction(std::function<void()> function,
                                   absl::Duration first_invocation_delay,
                                   absl::Duration invocation_interval)
    : function_(std::move(function)),
      first_invocation_delay_(first_invocation_delay),
      invocation_interval_(invocation_interval),
      reload_thread_(std::make_unique<std::thread>([this] {
        if (exit_notification_.WaitForNotificationWithTimeout(
                first_invocation_delay_)) {
          return;
        }
        do {
          function_();
        } while (!exit_notification_.WaitForNotificationWithTimeout(
            invocation_interval_));
      })) {}

PeriodicFunction::~PeriodicFunction() {
  exit_notification_.Notify();
  reload_thread_->join();
}

PeriodicFunctionFactory PeriodicFunction::DefaultFactory() {
  constexpr auto factory = [](std::function<void()> function,
                              absl::Duration first_invocation_interval,
                              absl::Duration invocation_interval) {
    return std::make_unique<PeriodicFunction>(
        std::move(function), first_invocation_interval, invocation_interval);
  };
  return factory;
}
}  // namespace aviary::util
