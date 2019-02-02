licenses(["notice"])

load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "frozy_cluster_lib",
    srcs = ["cluster.cc"],
    hdrs = ["cluster.h", "downstream_connection.h", "upstream_connection.h"],
    deps = [
        ":state_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/common:linked_object",
        "//source/common/config:utility_lib",
        "//source/common/network:address_lib",
        "//source/common/network:listen_socket_lib",
    ],
)


envoy_cc_library(
    name = "state_lib",
    srcs = ["state.cc"],
    hdrs = ["state.h", "downstream_connection.h", "upstream_connection.h", "forward_filter.h"],
    deps = [
        ":connection_wrap_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/server:filter_config_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:enum_to_int",
        "//source/common/common:logger_lib",
        "//source/common/common:thread_lib",
        "//source/extensions/frozy/common:control_lib",
    ],
)

envoy_cc_library(
    name = "connection_wrap_interface",
    hdrs = ["connection_wrap.h"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/network:connection_interface",
        "//source/common/common:logger_lib",
        "//source/common/buffer:buffer_lib",
    ],
)
