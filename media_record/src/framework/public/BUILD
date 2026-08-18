package(default_visibility = ["//visibility:public"])

# Umbrella public target for external consumers.
# Header-only skeleton for now (spec 001); the three capability repos are NOT
# linked here — cross-library linking is exercised by examples/tests.
cc_library(
    name = "media_record",
    hdrs = glob(["include/media_record/*.h"]),
    strip_include_prefix = "include",
    copts = [
        "-fvisibility=hidden",
        "-fvisibility-inlines-hidden",
    ],
    alwayslink = 1,
    visibility = ["//visibility:public"],
)

# Self-contained shared library for non-Bazel consumers (publish mechanism).
cc_binary(
    name = "media_record_shared",
    linkshared = True,
    linkstatic = True,
    copts = [
        "-DMEDIA_RECORD_SHARED_LIBRARY",
        "-fvisibility=hidden",
        "-fvisibility-inlines-hidden",
    ],
    deps = [
        ":media_record",
    ],
)
