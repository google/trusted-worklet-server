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

#ifndef V8_V8_PLATFORM_INITIALIZER_H_
#define V8_V8_PLATFORM_INITIALIZER_H_

namespace aviary {
namespace v8 {
// V8 can only be initialized once per process, even if it is disposed and shut
// down. This class manages a process-local instance of V8 to enforce this
// restriction. You can make as many instances of V8PlatformInitializer as you
// wish, though at least one must have been initialized before the first use of
// V8.
class V8PlatformInitializer {
 public:
  V8PlatformInitializer();
};
}  // namespace v8
}  // namespace aviary

#endif  // V8_V8_PLATFORM_INITIALIZER_H_
