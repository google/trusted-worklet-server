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

#include "gtest/gtest.h"

namespace aviary::util {
namespace {

TEST(PeriodicFunction, InvokeOnceAfterInitialDelay) {
  absl::Mutex mutex;
  int invocations = 0;
  auto periodic_function = PeriodicFunction::DefaultFactory()(
      [&] {
        absl::MutexLock lock(&mutex);
        invocations++;
      },
      absl::Milliseconds(100), absl::Seconds(1));
  EXPECT_EQ(invocations, 0);
  absl::SleepFor(absl::Milliseconds(50));
  EXPECT_EQ(invocations, 0);
  absl::SleepFor(absl::Milliseconds(200));
  EXPECT_EQ(invocations, 1);
}

TEST(PeriodicFunction, InvokeMultipleTimes) {
  absl::Mutex mutex;
  int invocations = 0;
  auto periodic_function = PeriodicFunction::DefaultFactory()(
      [&] {
        absl::MutexLock lock(&mutex);
        invocations++;
      },
      absl::Milliseconds(10), absl::Milliseconds(100));
  absl::SleepFor(absl::Milliseconds(350));
  // 4 invocations ones. 1 after the shorter initial delay of 10ms, 3 more after
  // invocation intervals of 100ms each.
  EXPECT_EQ(invocations, 4);
}

TEST(PeriodicFunction, SuccessfulDestruction) {
  absl::Mutex mutex;
  int invocations = 0;
  auto periodic_function = PeriodicFunction::DefaultFactory()(
      [&] {
        absl::MutexLock lock(&mutex);
        invocations++;
      },
      absl::Milliseconds(100), absl::Hours(1));
  // Give one invocation a chance to complete.
  absl::SleepFor(absl::Milliseconds(600));
  absl::Time desctruction_start = absl::Now();
  // Destroy PeriodicFunction.
  periodic_function.reset();
  absl::Duration destruction_wait = absl::Now() - desctruction_start;
  // Function should have been invoked once at creation time.
  EXPECT_EQ(invocations, 1);
  // Destruction should have taken trivial time. It should not have been blocked
  // on waiting for a full invocation interval.
  EXPECT_LE(destruction_wait, absl::Milliseconds(50));
}

TEST(PeriodicFunction, SuccessfulDestructionBeforeInitialInvocation) {
  absl::Mutex mutex;
  int invocations = 0;
  auto periodic_function = PeriodicFunction::DefaultFactory()(
      [&] {
        absl::MutexLock lock(&mutex);
        invocations++;
      },
      absl::Hours(1), absl::Hours(1));
  // Yield a thread.
  absl::SleepFor(absl::Milliseconds(100));
  absl::Time desctruction_start = absl::Now();
  periodic_function = nullptr;
  absl::Duration destruction_wait = absl::Now() - desctruction_start;
  // Function should not ever have been invoked.
  EXPECT_EQ(invocations, 0);
  // Destruction should have taken trivial time. It should not have been blocked
  // on waiting for the initial invocation delay.
  EXPECT_LE(destruction_wait, absl::Milliseconds(50));
}
}  // namespace
}  // namespace aviary::util
