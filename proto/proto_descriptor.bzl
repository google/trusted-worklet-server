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

def _paths(files):
    return [f.path for f in files]

def _impl(ctx):
    descriptors = ctx.attr.proto_library[ProtoInfo].transitive_descriptor_sets.to_list()
    ctx.actions.run_shell(
        inputs = descriptors,
        outputs = [ctx.outputs.out],
        command = "cat %s > %s" % (
            " ".join(_paths(descriptors)),
            ctx.outputs.out.path,
        ),
    )

# Produces a binary Protocol Buffer descriptor file for the proto target
# specified by proto_library
proto_descriptor = rule(
    implementation = _impl,
    attrs = {
        "proto_library": attr.label(),
        "out": attr.output(mandatory = True),
    },
)
