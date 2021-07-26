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

#include "util/status_encoding.h"

namespace aviary {
namespace util {
void SaveStatusToProto(const absl::Status& s, google::rpc::Status* proto) {
  proto->set_code(s.raw_code());
  if (!s.message().empty()) {
    proto->set_message(std::string(s.message()));
  }

  // Store non-MessageSet payloads as `google.protobuf.Any` in a unique entry
  // of MessageSet.
  s.ForEachPayload([&](absl::string_view type_url, const absl::Cord& payload) {
    google::protobuf::Any* any = proto->mutable_details()->Add();
    any->set_type_url(std::string(type_url));
    any->set_value(std::string(payload));
  });
}

absl::Status StatusFromProto(const google::rpc::Status& proto) {
  if (proto.code() == 0) {
    return absl::OkStatus();
  }
  absl::StatusCode canonical_code;
  if (proto.code()) {
    canonical_code = static_cast<absl::StatusCode>(proto.code());
  }

  absl::Status ret;
  ret = absl::Status(canonical_code, proto.message());
  if (proto.details().empty()) {
    return ret;
  }
  for (const auto& detail : proto.details()) {
    ret.SetPayload(detail.type_url(), absl::Cord(detail.value()));
  }
  return ret;
}
}  // namespace util
}  // namespace aviary
