licenses(["notice"])

load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

load(
    "@envoy_api//bazel:api_build_system.bzl",
    "api_proto_library",
)

envoy_package()

envoy_cc_library(
    name = "connector_lib",
    srcs = ["control_state.cc"],
    hdrs = ["control.h", "control_state.h", "upstream.h", "downstream.h"],
    deps = [
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/server:filter_config_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:linked_object",
        "//source/common/common:enum_to_int",
        "//source/common/common:logger_lib",
        "//source/extensions/frozy/common:control_lib",
    ],
)
