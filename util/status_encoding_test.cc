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

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/struct.pb.h"
#include "google/protobuf/util/message_differencer.h"
#include "google/rpc/status.pb.h"
#include "gtest/gtest.h"

namespace aviary {
namespace util {

namespace {

constexpr absl::string_view kMessage = "Wrong credentials";

google::protobuf::Struct SamplePayload() {
  google::protobuf::Struct payload;
  (*payload.mutable_fields())["numeric_field"].set_number_value(42.0);
  return payload;
}

TEST(StatusEncodingTest, SymmetricConversion) {
  auto status = absl::NotFoundError("Entity not found");
  status.SetPayload("type.googleapis.com/google.protobuf.Struct",
                    absl::Cord(SamplePayload().SerializeAsString()));
  google::rpc::Status status_proto;
  SaveStatusToProto(status, &status_proto);
  EXPECT_EQ(StatusFromProto(status_proto), status);
}

TEST(StatusEncodingTest, SymmetricConversionCodeOnly) {
  auto status = absl::InvalidArgumentError("");
  google::rpc::Status status_proto;
  SaveStatusToProto(status, &status_proto);
  EXPECT_EQ(StatusFromProto(status_proto), status);
}

TEST(StatusEncodingTest, SymmetricConversionOkStatus) {
  auto status = absl::OkStatus();
  google::rpc::Status status_proto;
  SaveStatusToProto(status, &status_proto);
  EXPECT_EQ(StatusFromProto(status_proto), status);
}

TEST(StatusEncodingTest, SaveStatusToProto) {
  auto status = absl::PermissionDeniedError(kMessage);
  google::protobuf::Struct payload = SamplePayload();
  status.SetPayload("type.googleapis.com/google.protobuf.Struct",
                    absl::Cord(payload.SerializeAsString()));
  google::rpc::Status status_proto;
  SaveStatusToProto(status, &status_proto);
  EXPECT_EQ(status_proto.code(),
            static_cast<int>(absl::StatusCode::kPermissionDenied));
  EXPECT_EQ(status_proto.message(), kMessage);
  EXPECT_EQ(status_proto.details_size(), 1);
  google::protobuf::Struct actual_payload;
  EXPECT_TRUE(status_proto.details(0).UnpackTo(&actual_payload));
  EXPECT_TRUE(google::protobuf::util::MessageDifferencer::Equals(
      payload, actual_payload));
}

TEST(StatusEncodingTest, StatusFromProto) {
  google::rpc::Status status_proto;
  status_proto.set_message(std::string(kMessage));
  status_proto.set_code(static_cast<int>(absl::StatusCode::kPermissionDenied));
  status_proto.add_details()->PackFrom(SamplePayload());
  auto expected_status = absl::PermissionDeniedError(kMessage);
  expected_status.SetPayload("type.googleapis.com/google.protobuf.Struct",
                             absl::Cord(SamplePayload().SerializeAsString()));
  EXPECT_EQ(StatusFromProto(status_proto), expected_status);
}
}  // namespace
}  // namespace util
}  // namespace aviary
