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

FROM launcher.gcr.io/google/ubuntu1804

ARG DOCKER_VERSION=5:19.03.8~3-0~ubuntu-bionic

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
    # Install Git >2.0.1
    add-apt-repository ppa:git-core/ppa && \
    apt-get -y update && \
    apt-get -y install git && \
    add-apt-repository ppa:ubuntu-toolchain-r/test -y && \
    apt-get -y update && \
    apt-get -y install g++-9 gcc-9 && \
    # Install Bazel 3.5.0 (https://docs.bazel.build/versions/master/install-ubuntu.html)
    apt-get -y install openjdk-8-jdk && \
    echo "deb [arch=amd64] http://storage.googleapis.com/bazel-apt stable jdk1.8" | tee /etc/apt/sources.list.d/bazel.list && \
    curl https://bazel.build/bazel-release.pub.gpg | apt-key add - && \
    apt-get update && \
    apt-get -y install bazel-3.5.0 && \
    apt-get -y upgrade bazel-3.5.0 && \
    # Install Docker (https://docs.docker.com/engine/installation/linux/docker-ce/ubuntu/#uninstall-old-versions)
    apt-get -y install linux-image-extra-virtual && \
    curl -fsSL https://download.docker.com/linux/ubuntu/gpg | apt-key add - && \
    add-apt-repository \
      "deb [arch=amd64] https://download.docker.com/linux/ubuntu \
      $(lsb_release -cs) \
      stable edge" && \
    apt-get -y update && \
    apt-get install -y docker-ce=${DOCKER_VERSION} docker-ce-cli=${DOCKER_VERSION} unzip && \
    bazel-3.5.0 version && \
    apt-get -y install python3-pip && \
    # Python clang package is used by Sandbox2 build process
    pip3 install clang && \
    apt-get -y install libclang-dev && \
    apt-get install -y lsb-release sudo
RUN update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-9 9 && \
    update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-9 9
# Install Chromium/V8 build dependencies.
RUN \
  DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends tzdata
RUN ["/bin/bash", "-c", "source <(curl https://chromium.googlesource.com/chromium/src/+/refs/heads/master/build/install-build-deps.sh?format=TEXT | base64 --decode) --no-nacl --no-prompt --no-arm --no-chromeos-fonts"]

# Store the Bazel outputs under /workspace so that the symlinks under bazel-bin
# (et al) are accessible to downstream build steps.
RUN mkdir -p /workspace
RUN echo "startup --output_base=/workspace/.bazel" > ~/.bazelrc

# Create a separate user to run Bazel under.
# Some of the build steps do not work well under root user.
RUN groupadd --gid 5000 bazel \
    && useradd --home-dir /home/bazel --create-home --uid 5000 \
        --gid 5000 --shell /bin/bash --skel /dev/null bazel

# Unpack bazel for future use.
RUN bazel-3.5.0 version

COPY aviary-bazel.sh /builder/aviary-bazel.sh
RUN chmod +x /builder/aviary-bazel.sh
ENTRYPOINT ["/builder/aviary-bazel.sh"]
