licenses(["notice"])

exports_files(["LICENSE"])

cc_library(
    name = "cpp_httplib",
    hdrs = ["httplib.h"],
    visibility = ["//visibility:public"],
    defines = ["CPPHTTPLIB_OPENSSL_SUPPORT"],
    deps = ["@boringssl//:ssl"],
)
