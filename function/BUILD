load("@com_google_sandboxed_api//sandboxed_api/bazel:build_defs.bzl", "sapi_platform_copts")
load("@com_google_sandboxed_api//sandboxed_api/bazel:embed_data.bzl", "sapi_cc_embed_data")

cc_library(
    name = "bidding_function_interface",
    hdrs = ["bidding_function_interface.h"],
    visibility = [
        "//server:__subpackages__",
    ],
    deps = ["//proto:bidding_function_cc_proto"],
)

cc_library(
    name = "bidding_function",
    srcs = ["bidding_function.cc"],
    hdrs = ["bidding_function.h"],
    visibility = [
        "//server:__subpackages__",
    ],
    deps = [
        ":bidding_function_interface",
        "//proto:bidding_function_cc_proto",
        "//util:status_macros",
        "//v8:v8_platform_initializer",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@com_google_protobuf//:protobuf",
        "@v8",
    ],
)

cc_test(
    name = "bidding_function_test",
    srcs = [
        "bidding_function_test.cc",
    ],
    deps = [
        ":bidding_function",
        ":sapi_bidding_function",
        "//proto:bidding_function_cc_proto",
        "//util:parse_proto",
        "@com_google_absl//absl/strings",
        "@com_google_protobuf//:protobuf",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "bidding_function_sapi_adapter",
    srcs = ["bidding_function_sapi_adapter.cc"],
    hdrs = ["bidding_function_sapi_adapter.h"],
    copts = sapi_platform_copts(),
    visibility = ["//function:__subpackages__"],
    deps = [
        ":bidding_function",
        ":bidding_function_interface",
        ":bidding_function_sandbox_cc_proto",
        "//proto:bidding_function_cc_proto",
        "//util:status_encoding",
        "//util:status_macros",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status:statusor",
    ],
    alwayslink = 1,  # All functions are linked into dependent binaries
)

cc_binary(
    name = "bidding_function_sapi_adapter_bin",
    srcs = ["bidding_function_sapi_adapter_main.cc"],
    # See https://developers.google.com/sandboxed-api/docs/sandbox2/faq#how_should_i_compile_my_sandboxee
    features = [
        "fully_static_link",  # link libc statically
        "-pie",
    ],
    linkopts = ["-l:libstdc++.a"],
    linkstatic = 1,
    deps = [
        ":bidding_function_sapi_adapter",
        "//v8:v8_platform_initializer",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2:comms",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2:forkingclient",
    ],
)

sapi_cc_embed_data(
    name = "bidding_function_sapi_adapter_bin_embed",
    srcs = [":bidding_function_sapi_adapter_bin"],
    namespace = "aviary::function",
)

proto_library(
    name = "bidding_function_sandbox_proto",
    srcs = ["bidding_function_sandbox.proto"],
    deps = [
        "//proto:bidding_function_proto",
        "@com_google_googleapis//google/rpc:status_proto",
        "@com_google_protobuf//:any_proto",
    ],
)

cc_proto_library(
    name = "bidding_function_sandbox_cc_proto",
    deps = [":bidding_function_sandbox_proto"],
)

cc_library(
    name = "sapi_bidding_function",
    srcs = ["sapi_bidding_function.cc"],
    hdrs = ["sapi_bidding_function.h"],
    visibility = [
        "//server:__subpackages__",
    ],
    deps = [
        ":bidding_function_interface",
        ":bidding_function_sandbox_cc_proto",
        ":bidding_function_sapi_adapter",
        ":bidding_function_sapi_adapter_bin_embed",
        "//util:status_encoding",
        "//util:status_macros",
        "@com_google_absl//absl/status",
        "@com_google_sandboxed_api//sandboxed_api:sapi",
    ],
)
