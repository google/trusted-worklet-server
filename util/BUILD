package(
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "status_macros",
    hdrs = ["status_macros.h"],
    deps = [
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
    ],
)

cc_library(
    name = "parse_proto",
    hdrs = ["parse_proto.h"],
    deps = [
        "@com_google_absl//absl/strings",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "unused_port",
    srcs = ["unused_port.cc"],
    hdrs = ["unused_port.h"],
    deps = [
        "@com_google_absl//absl/cleanup",
        "@com_google_absl//absl/random",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/synchronization",
    ],
)

cc_library(
    name = "status_encoding",
    srcs = ["status_encoding.cc"],
    hdrs = ["status_encoding.h"],
    deps = [
        "@com_google_absl//absl/status",
        "@com_google_googleapis//google/rpc:status_cc_proto",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "status_encoding_test",
    srcs = ["status_encoding_test.cc"],
    deps = [
        ":status_encoding",
        "@com_google_absl//absl/status",
        "@com_google_googleapis//google/rpc:status_cc_proto",
        "@com_google_protobuf//:protobuf",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "periodic_function",
    srcs = ["periodic_function.cc"],
    hdrs = ["periodic_function.h"],
    deps = [
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
    ],
)

cc_test(
    name = "periodic_function_test",
    srcs = ["periodic_function_test.cc"],
    deps = [
        ":periodic_function",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "test_periodic_function",
    testonly = 1,
    srcs = ["test_periodic_function.cc"],
    hdrs = ["test_periodic_function.h"],
    deps = [
        ":periodic_function",
        "@com_google_absl//absl/time",
    ],
)
