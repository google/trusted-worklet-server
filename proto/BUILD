load("proto_descriptor.bzl", "proto_descriptor")
load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_proto_library")
load("@com_github_grpc_grpc//bazel:cc_grpc_library.bzl", "cc_grpc_library")

proto_library(
    name = "aviary_proto",
    srcs = ["aviary.proto"],
    deps = [
        ":bidding_function_proto",
        "@com_google_googleapis//google/api:annotations_proto",
        "@com_google_protobuf//:struct_proto",
    ],
)

proto_descriptor(
    name = "aviary_proto_descriptor",
    out = "aviary_descriptor.pb",
    proto_library = ":aviary_proto",
    visibility = ["//server:__subpackages__"],
)

cc_proto_library(
    name = "aviary_cc_proto",
    deps = [":aviary_proto"],
)

cc_grpc_library(
    name = "aviary_cc_grpc",
    srcs = [":aviary_proto"],
    grpc_only = True,
    visibility = ["//server:__subpackages__"],
    deps = [":aviary_cc_proto"],
)

proto_library(
    name = "bidding_function_proto",
    srcs = ["bidding_function.proto"],
    visibility = [
        "//function:__subpackages__",
        "//server:__subpackages__",
    ],
    deps = [
        "@com_google_protobuf//:struct_proto",
    ],
)

cc_proto_library(
    name = "bidding_function_cc_proto",
    visibility = [
        "//function:__subpackages__",
        "//server:__subpackages__",
    ],
    deps = [":bidding_function_proto"],
)
