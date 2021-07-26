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

#include "v8/v8_platform_initializer.h"

#include "absl/base/call_once.h"
#include "libplatform/libplatform.h"
#include "v8.h"

namespace aviary {
namespace v8 {
namespace {
class V8PlatformInitializerImpl {
 public:
  V8PlatformInitializerImpl()
      : platform_(::v8::platform::NewDefaultPlatform()) {
    ::v8::V8::InitializePlatform(platform_.get());
    ::v8::V8::Initialize();
  }

  // In practice we will never invoke this destructor, but that's ok since we
  // generally won't dispose of V8 until shutdown. Leaving this code here as
  // reference in case the need ever arises for us to reclaim V8 resources while
  // keeping the process alive.
  ~V8PlatformInitializerImpl() {
    ::v8::V8::Dispose();
    ::v8::V8::ShutdownPlatform();
  }

 private:
  std::unique_ptr<::v8::Platform> platform_;
};

V8PlatformInitializerImpl* InitializerInstance() {
  static V8PlatformInitializerImpl* instance = new V8PlatformInitializerImpl();
  return instance;
}

}  // namespace

V8PlatformInitializer::V8PlatformInitializer() { InitializerInstance(); }
}  // namespace v8
}  // namespace aviary
