##### Protobuf Rules for Bazel
##### See https://github.com/bazelbuild/rules_proto
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")
load("@bazel_tools//tools/build_defs/repo:utils.bzl", "maybe")
load("//v8:v8.bzl", "v8_sources")

v8_sources(
    name = "v8",
    branch = "9.0-lkgr",
)

# Abseil library repository
# Needs to precede the reference to Protocol Buffers repository due to a conflict.
git_repository(
    name = "com_google_absl",
    commit = "2e9532cc6c701a8323d0cffb468999ab804095ab",
    remote = "https://github.com/abseil/abseil-cpp",
    shallow_since = "1615394410 -0500",
)

git_repository(
    name = "com_google_sandboxed_api",
    commit = "bc9d7a8db628d77adbc738ee11a63ceac6c3fe39",
    remote = "https://github.com/google/sandboxed-api",
    shallow_since = "1616586496 -0700",
)

git_repository(
    name = "boringssl",
    commit = "eca60502377960990081cbdabceeaa8d2d15e1a5",
    remote = "https://boringssl.googlesource.com/boringssl",
)

load("@com_google_sandboxed_api//sandboxed_api/bazel:sapi_deps.bzl", "sapi_deps")

sapi_deps()

http_archive(
    name = "rules_python",
    sha256 = "b6d46438523a3ec0f3cead544190ee13223a52f6a6765a29eae7b7cc24cc83a0",
    url = "https://github.com/bazelbuild/rules_python/releases/download/0.1.0/rules_python-0.1.0.tar.gz",
)

http_archive(
    name = "rules_proto",
    sha256 = "d8992e6eeec276d49f1d4e63cfa05bbed6d4a26cfe6ca63c972827a0d141ea3b",
    strip_prefix = "rules_proto-cfdc2fa31879c0aebe31ce7702b1a9c8a4be02d2",
    urls = [
        "https://mirror.bazel.build/github.com/bazelbuild/rules_proto/archive/cfdc2fa31879c0aebe31ce7702b1a9c8a4be02d2.tar.gz",
        "https://github.com/bazelbuild/rules_proto/archive/cfdc2fa31879c0aebe31ce7702b1a9c8a4be02d2.tar.gz",
    ],
)

load("@rules_proto//proto:repositories.bzl", "rules_proto_dependencies", "rules_proto_toolchains")

rules_proto_dependencies()

rules_proto_toolchains()

##### gRPC Rules for Bazel
##### See https://github.com/grpc/grpc/blob/master/src/cpp/README.md#make
http_archive(
    name = "com_github_grpc_grpc",
    sha256 = "61272ea6d541f60bdc3752ddef9fd4ca87ff5ab18dd21afc30270faad90c8a34",
    strip_prefix = "grpc-de893acb6aef88484a427e64b96727e4926fdcfd",
    urls = [
        "https://github.com/grpc/grpc/archive/de893acb6aef88484a427e64b96727e4926fdcfd.tar.gz",
    ],
)

load("@com_github_grpc_grpc//bazel:grpc_deps.bzl", "grpc_deps")

grpc_deps()

# Not mentioned in official docs... mentioned here https://github.com/grpc/grpc/issues/20511
load("@com_github_grpc_grpc//bazel:grpc_extra_deps.bzl", "grpc_extra_deps")

grpc_extra_deps()

# Protobuf repo for protoc, Protobuf runtime and well-known types
http_archive(
    name = "com_google_protobuf",
    sha256 = "cef7f1b5a7c5fba672bec2a319246e8feba471f04dcebfe362d55930ee7c1c30",
    strip_prefix = "protobuf-3.5.0",
    urls = ["https://github.com/google/protobuf/archive/v3.5.0.zip"],
)

git_repository(
    name = "yaml-cpp",
    commit = "a6bbe0e50ac4074f0b9b44188c28cf00caf1a723",
    remote = "https://github.com/jbeder/yaml-cpp",
    shallow_since = "1609854028 -0600",
)

git_repository(
    name = "googletest",
    commit = "703bd9caab50b139428cea1aaff9974ebee5742e",
    remote = "https://github.com/google/googletest",
    shallow_since = "1570114335 -0400",
)

http_archive(
    name = "subprocess",
    build_file = "@//third_party:subprocess.BUILD",
    sha256 = "19d865146d8565969da659ffdf9cf5feac5c36d02b52a6a687e41bd53114645f",
    strip_prefix = "subprocess-0.4.0/src/cpp",
    urls = ["https://github.com/benman64/subprocess/archive/v0.4.0.tar.gz"],
)

http_archive(
    name = "cpp_httplib",
    build_file = "@//third_party:cpp_httplib.BUILD",
    sha256 = "b353f3e7c124a08940d9425aeb7206183fa29857a8f720c162f8fd820cc18f0e",
    strip_prefix = "cpp-httplib-0.8.5",
    urls = ["https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.8.5.tar.gz"],
)

http_archive(
    name = "com_google_googleapis",
    sha256 = "8605d58db78796469f7906d0c3aa5547f6b6296e1dd13e5a55dfffc6ae6f0c8c",
    strip_prefix = "googleapis-13b6ba5e35620d15a97ae0f8c38be0c0ff1c2d42",
    urls = ["https://github.com/googleapis/googleapis/archive/13b6ba5e35620d15a97ae0f8c38be0c0ff1c2d42.zip"],
)

load("@com_google_googleapis//:repository_rules.bzl", "switched_rules_by_language")

switched_rules_by_language(
    name = "com_google_googleapis_imports",
    cc = True,
    grpc = True,
)

http_archive(
    name = "rules_pkg",
    sha256 = "6b5969a7acd7b60c02f816773b06fcf32fbe8ba0c7919ccdc2df4f8fb923804a",
    urls = [
        "https://mirror.bazel.build/github.com/bazelbuild/rules_pkg/releases/download/0.3.0/rules_pkg-0.3.0.tar.gz",
        "https://github.com/bazelbuild/rules_pkg/releases/download/0.3.0/rules_pkg-0.3.0.tar.gz",
    ],
)

load("@rules_pkg//:deps.bzl", "rules_pkg_dependencies")

rules_pkg_dependencies()

http_archive(
    name = "io_bazel_rules_docker",
    patches = ["//:rules_docker.patch"],
    sha256 = "95d39fd84ff4474babaf190450ee034d958202043e366b9fc38f438c9e6c3334",
    strip_prefix = "rules_docker-0.16.0",
    urls = ["https://github.com/bazelbuild/rules_docker/releases/download/v0.16.0/rules_docker-v0.16.0.tar.gz"],
)

load(
    "@io_bazel_rules_docker//toolchains/docker:toolchain.bzl",
    docker_toolchain_configure = "toolchain_configure",
)

docker_toolchain_configure(
    name = "docker_config",
)

load(
    "@io_bazel_rules_docker//repositories:repositories.bzl",
    container_repositories = "repositories",
)

container_repositories()

load("@io_bazel_rules_docker//repositories:deps.bzl", container_deps = "deps")

container_deps()

load("@io_bazel_rules_docker//container:pull.bzl", "container_pull")

container_pull(
    name = "aviary-base",
    digest = "sha256:d1c0d917e52d0ed9e9e2949ff7293b8b06c8aa6542a956aad76213faf59275b4",
    registry = "gcr.io",
    repository = "ads-trusted-server-dev/aviary-base",
    tag = "latest",
)
