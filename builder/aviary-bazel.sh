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

# Invokes Bazel under 'bazel' user and passes all arguments received as is.
set -e
chown -R :bazel /workspace
# Get Docker group name or GID from the mounted /var/run/docker.sock
# and add bazel user to the group that owns /var/run/docker.sock to make
# it possible to use Docker while running as that user.
DOCKER_GROUP=$(stat -c '%G' /var/run/docker.sock)
if [ $DOCKER_GROUP == "UNKNOWN" ]; then
  DOCKER_GID=$(stat -c '%g' /var/run/docker.sock)
  groupadd --gid $DOCKER_GID dockerhost
  usermod -a -G dockerhost bazel
else
  usermod -a -G ${DOCKER_GROUP} bazel
fi
# Copy Docker configuration (including credentials) to the bazel user home
# directory. Docker registry credentials can be made available by the
# Google Cloud Build.
cp -R $HOME/.docker /home/bazel/.docker
chown -R bazel:bazel /home/bazel/.docker
COMMAND="cd /workspace; bazel-3.5.0 $@"
su -s /bin/bash -c "${COMMAND}" - bazel
