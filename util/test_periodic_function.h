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

#ifndef UTIL_TEST_PERIODIC_FUNCTION_H_
#define UTIL_TEST_PERIODIC_FUNCTION_H_

#include "util/periodic_function.h"

namespace aviary::util {

// A test version of the PeriodicFunction that allows triggering the execution
// of the provided function immediately on the calling thread.
class TestPeriodicFunction : public PeriodicFunction {
 public:
  explicit TestPeriodicFunction(std::function<void()> function);

  void InvokeNow() const;

  TestPeriodicFunction(const TestPeriodicFunction&) = delete;
  TestPeriodicFunction& operator=(const TestPeriodicFunction&) = delete;
};

// Container holds multiple TestPeriodicFunctions and allows to trigger an
// immediate execution of the underlying callbacks. Used for testing.
class TestPeriodicFunctionContainer {
 public:
  TestPeriodicFunctionContainer();
  PeriodicFunctionFactory Factory();

  void InvokeAllNow();

 private:
  std::vector<const TestPeriodicFunction*> instances_;
  PeriodicFunctionFactory factory_;
};

}  // namespace aviary::util

#endif  // UTIL_TEST_PERIODIC_FUNCTION_H_
