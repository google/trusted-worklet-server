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

#include "util/unused_port.h"

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <fstream>
#include <iosfwd>

#include "absl/cleanup/cleanup.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"

namespace aviary {
namespace util {
namespace {
bool IsPortAvailable(int port) {
  auto fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd == -1) {
    return false;
  }
  absl::Cleanup fd_closer = [&fd] {
    shutdown(fd, SHUT_RDWR);
    close(fd);
  };
  int optval = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
    return false;
  }
  sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = port;
  addr.sin_addr.s_addr = 0;

  if (bind(fd, (struct sockaddr*)&addr, sizeof(sockaddr_in)) == -1) {
    return false;
  }
  if (listen(fd, 1) == -1) {
    return false;
  }
  return true;
}

absl::Mutex last_port_mutex_;
int next_port_ = 0;
}  // namespace

absl::StatusOr<int> FindUnusedPort() {
  std::ifstream port_range_file("/proc/sys/net/ipv4/ip_local_port_range");
  constexpr int kFirstUserPort = 1024;
  constexpr int kMaxTries = 10;
  // We avoid the use of ephemeral ports as these can be frequently occupied by
  // client connections.
  int min_ephemeral_port, max_ephemeral_port;
  if (!(port_range_file >> min_ephemeral_port >> max_ephemeral_port)) {
    return absl::ResourceExhaustedError("Unable to get ephemeral port range.");
  }
  int port;
  {
    absl::ReaderMutexLock lock(&last_port_mutex_);
    port = next_port_;
  }
  if (port == 0) {
    port = kFirstUserPort;
  }
  for (int tries = 0; tries < kMaxTries; tries++) {
    if (IsPortAvailable(port)) {
      absl::WriterMutexLock lock(&last_port_mutex_);
      next_port_ = port + 1;
      return port;
    }
    if (++port >= min_ephemeral_port) {
      port = kFirstUserPort;
    }
  }
  return absl::ResourceExhaustedError("Unable to find an unused TCP port.");
}
}  // namespace util
};  // namespace aviary