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

# Base image for running Aviary. Includes the latest version of Envoy proxy
# (https://www.envoyproxy.io/) and the up-to-date version of libstdc++.
FROM index.docker.io/envoyproxy/envoy-dev

RUN \
    # This makes add-apt-repository available.
    apt-get update && \
    apt-get -y install \
        python \
        python-pkg-resources \
        software-properties-common \
        unzip \
        apt-transport-https \
        curl \
        ca-certificates && \
    add-apt-repository ppa:ubuntu-toolchain-r/test -y && \
    apt-get -y update && \
    apt-get -y upgrade libstdc++6
