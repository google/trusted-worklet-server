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

#include "util/test_periodic_function.h"

namespace aviary::util {

TestPeriodicFunction::TestPeriodicFunction(std::function<void()> function)
    : PeriodicFunction(std::move(function),
                       /*first_invocation_delay=*/absl::InfiniteDuration(),
                       /*invocation_interval=*/absl::InfiniteDuration()) {}

void TestPeriodicFunction::InvokeNow() const { function_(); }

TestPeriodicFunctionContainer::TestPeriodicFunctionContainer()
    : factory_([this](std::function<void()> function,
                      absl::Duration first_invocation_delay,
                      absl::Duration invocation_interval) {
        auto periodic_function =
            std::make_unique<TestPeriodicFunction>(std::move(function));
        instances_.push_back(periodic_function.get());
        return periodic_function;
      }) {}

PeriodicFunctionFactory TestPeriodicFunctionContainer::Factory() {
  return factory_;
}

void TestPeriodicFunctionContainer::InvokeAllNow() {
  for (auto& function_instance : instances_) {
    function_instance->InvokeNow();
  }
}
}  // namespace aviary::util
