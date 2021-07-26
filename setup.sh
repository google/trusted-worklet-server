#!/bin/bash
# Copyright 2021 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -e

# Install Docker.
echo "Installing Docker."
curl -fsSL https://get.docker.com -o get-docker.sh
chmod +x ./get-docker.sh
./get-docker.sh

# Add the current user to the docker group.
USERMOD_COMMAND="sudo -E sh -c \"usermod -aG docker $USER\""
echo "Adding the current user to the docker group with: "
echo "$USERMOD_COMMAND. Please enter root password if prompted."
eval "${USERMOD_COMMAND}"

# Install Bazelisk (wrapper around Bazel).
echo "Installing Bazelisk."
go get github.com/bazelbuild/bazelisk
echo "export PATH=$PATH:$(go env GOPATH)/bin" >> ~/.bash_profile

# Install Chromium/V8 build dependencies.
echo "Installing Chromium/V8 build dependencies."
curl https://chromium.googlesource.com/chromium/src/+/refs/heads/master/build/install-build-deps.sh?format=TEXT | base64 --decode > ./install-build-deps.sh
chmod +x ./install-build-deps.sh
./install-build-deps.sh --no-chromeos-fonts
