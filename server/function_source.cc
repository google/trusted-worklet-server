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

#include "server/function_source.h"

#include <regex>

#include "absl/strings/str_cat.h"
#include "httplib.h"

namespace aviary {
namespace server {
namespace {
absl::StatusOr<std::string> TranslateResponse(const httplib::Result& res) {
  switch (res->status) {
    case 200:
      return res->body;
    case 400:
      return absl::InvalidArgumentError(
          "The server returned 400 Bad Request status code.");
    case 401:
    case 403:
      return absl::PermissionDeniedError(absl::StrCat(
          "Unauthenticated or unauthorized request. HTTP status code:",
          res->status));
    case 404:
      return absl::NotFoundError("Resource at the URL was not found.");
    default:
      return absl::InternalError("Unable to fetch a URL");
  }
}

absl::Status InvalidRemoteUriError(const FunctionSpecification& specification) {
  return absl::InvalidArgumentError(
      absl::StrCat("Not a valid remote URL: ", specification.uri));
}
}  // namespace

absl::StatusOr<std::string> FunctionSource::GetFunctionCode(
    const aviary::server::FunctionSpecification& specification) const {
  // Regular expression that allows to split a URL (if well-formed) into its
  // constituent parts (scheme, authority comprised of a host and an optional
  // port and a path).
  // Used to split the URL into 2 parts: (1) scheme-host-port and (2) path.
  const static std::regex url_re(
      // Start of the scheme, host and port group (1)
      "^("
      // Scheme (2)
      "(?:([a-z]+)://)?"
      // Host (3)
      "([^:/?#]+)"
      // Port (4)
      R"((?::(\d+))?)"
      ")"  // End of the scheme, host and port group
      // Path (5)
      "(/.*)?");
  constexpr static int kSchemeHostPortGroup = 1;
  constexpr static int kSchemeGroup = 2;
  constexpr static int kPathGroup = 5;
  std::smatch m;
  if (!std::regex_match(specification.uri, m, url_re)) {
    return absl::InvalidArgumentError(
        absl::StrCat("Not a valid URL: ", specification.uri));
  }
  auto scheme = m[kSchemeGroup].str();
  if (scheme.empty()) {
    return InvalidRemoteUriError(specification);
  }
  if (scheme == "local") {
    if (!specification.source_code) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Function source code not provided for local function."));
    }
    return *specification.source_code;
  }
  if (m.size() < kPathGroup) {
    return InvalidRemoteUriError(specification);
  }
  auto scheme_host_port = m[kSchemeHostPortGroup].str();
  auto path = m[kPathGroup].str();
  httplib::Client cli(scheme_host_port.c_str());
  auto res = cli.Get(path.c_str());
  if (!res) {
    return absl::InternalError("Unable to fetch a URL");
  }
  return TranslateResponse(res);
}
}  // namespace server
}  // namespace aviary
