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

#ifndef UTIL_PERIODIC_FUNCTION_H_
#define UTIL_PERIODIC_FUNCTION_H_

#include <functional>
#include <memory>
#include <thread>

#include "absl/synchronization/notification.h"
#include "absl/time/time.h"

namespace aviary::util {

// Forward declaration used in PeriodicFunctionFactory type alias below.
class PeriodicFunction;

using PeriodicFunctionFactory = std::function<std::unique_ptr<PeriodicFunction>(
    std::function<void()>, absl::Duration, absl::Duration)>;

// Periodically executes a given callback expressed as std::function with the
// given first invocation delay and invocation interval.
class PeriodicFunction {
 public:
  // Creates a periodic function. A function gets scheduled for the first
  // invocation after `first_invocation_delay` and subsequently gets invoked
  // every `invocation_interval`. `invocation_interval` is measured between the
  // end of the previous invocation and the start of the next one.
  PeriodicFunction(std::function<void()> function,
                   absl::Duration first_invocation_delay,
                   absl::Duration invocation_interval);
  virtual ~PeriodicFunction();

  static PeriodicFunctionFactory DefaultFactory();

  PeriodicFunction(const PeriodicFunction&) = delete;
  PeriodicFunction& operator=(const PeriodicFunction&) = delete;

 protected:
  const std::function<void()> function_;
  const absl::Duration first_invocation_delay_;
  const absl::Duration invocation_interval_;
  absl::Notification exit_notification_;
  const std::unique_ptr<std::thread> reload_thread_;
};

}  // namespace aviary::util

#endif  // UTIL_PERIODIC_FUNCTION_H_
