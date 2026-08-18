package(default_visibility = ["//visibility:public"])

licenses(["notice"])  # MIT

cc_library(
    name = "stb_image_write",
    srcs = ["stb_image_write.c"],
    hdrs = ["stb_image_write.h"],
    copts = ["-Wno-unused-function"],
    includes = ["."],
)

cc_library(
    name = "stb_image",
    srcs = ["stb_image_impl.c"],
    hdrs = ["stb_image.h"],
    copts = [
        "-Wno-unused-but-set-variable",
        "-Wno-unused-function",
    ],
    includes = ["."],
)
