load("@io_bazel_rules_docker//container:container.bzl", "container_image", "container_push")
load("@rules_pkg//:pkg.bzl", "pkg_tar")

cc_library(
    name = "function_repository",
    srcs = ["function_repository.cc"],
    hdrs = ["function_repository.h"],
    deps = [
        "//function:bidding_function_interface",
        "//proto:bidding_function_cc_proto",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "function_repository_test",
    srcs = ["function_repository_test.cc"],
    deps = [
        ":function_repository",
        "//function:bidding_function",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "function_source",
    srcs = ["function_source.cc"],
    hdrs = ["function_source.h"],
    deps = [
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:optional",
        "@cpp_httplib",
    ],
)

cc_test(
    name = "function_source_test",
    srcs = ["function_source_test.cc"],
    deps = [
        ":function_source",
        "//util:unused_port",
        "//v8:v8_platform_initializer",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "ad_auctions",
    srcs = ["ad_auctions.cc"],
    hdrs = ["ad_auctions.h"],
    deps = [
        ":function_repository",
        ":function_source",
        "//function:bidding_function",
        "//function:bidding_function_interface",
        "//function:sapi_bidding_function",
        "//proto:aviary_cc_grpc",
        "//proto:bidding_function_cc_proto",
        "//util:periodic_function",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/functional:bind_front",
        "@com_google_absl//absl/synchronization",
        "@cpp_httplib",
        "@yaml-cpp",
    ],
)

cc_test(
    name = "ad_auctions_test",
    srcs = ["ad_auctions_test.cc"],
    deps = [
        ":ad_auctions",
        "//proto:aviary_cc_grpc",
        "//util:parse_proto",
        "//util:test_periodic_function",
        "//v8:v8_platform_initializer",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/random",
        "@com_google_absl//absl/strings",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_binary(
    name = "server",
    srcs = ["server.cc"],
    defines = ["BAZEL_BUILD"],
    deps = [
        ":ad_auctions",
        ":function_source",
        "//proto:aviary_cc_grpc",
        "//v8:v8_platform_initializer",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_grpc_grpc//:grpc++_reflection",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "server_test",
    srcs = ["server_test.cc"],
    data = [
        ":server",
    ],
    deps = [
        "//proto:aviary_cc_grpc",
        "//util:parse_proto",
        "//util:unused_port",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@cpp_httplib",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
        "@subprocess",
    ],
)

pkg_tar(
    name = "envoy_configuration",
    srcs = [
        "envoy.tpl.yaml",
        "//proto:aviary_proto_descriptor",
    ],
    package_dir = "/etc/envoy",
    strip_prefix = ".",
)

pkg_tar(
    name = "aviary_configuration",
    srcs = [":sample_configuration.yaml"],
    package_dir = "/etc/aviary",
    strip_prefix = ".",
)

pkg_tar(
    name = "entrypoint",
    srcs = [
        ":entrypoint.sh",
        ":server",
    ],
    package_dir = "/usr/local/bin",
    strip_prefix = ".",
)

container_image(
    name = "aviary",
    base = "@aviary-base//image",
    cmd = [
        "/bin/sh",
        "-c",
        "/usr/local/bin/entrypoint.sh",
    ],
    tars = [
        ":envoy_configuration",
        ":entrypoint",
        ":aviary_configuration",
    ],
)

container_push(
    name = "push_aviary",
    format = "Docker",
    image = ":aviary",
    registry = "gcr.io",
    repository = "ads-trusted-server-dev/aviary",
    tag = "latest",
)
