licenses(["notice"])

load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "control_lib",
    hdrs = ["control.h"],
    deps = [
        "//include/envoy/network:connection_interface",
        "//source/common/buffer:buffer_lib",
    ],
)
