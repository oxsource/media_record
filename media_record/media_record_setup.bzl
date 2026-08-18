"""One-call transitive setup for media_record.

Loaded from WORKSPACE AFTER media_record_deps() has registered the three
external repos (Bazel requires load() at the top of a file, so the external
setup functions can only be referenced once their repos exist).

    load("//:media_record_deps.bzl", "media_record_deps")
    media_record_deps()
    load("//:media_record_setup.bzl", "media_record_setup")
    media_record_setup()
"""

load("@graph_runtime//:graph_runtime_deps.bzl", "graph_runtime_setup")
load("@native_ui//:native_ui_deps.bzl", "native_ui_setup")
load("@video_codec//:video_codec_deps.bzl", "video_codec_setup")


def media_record_setup():
    """Pulls each repo's own transitive http_archive dependencies."""
    graph_runtime_setup()
    native_ui_setup()
    video_codec_setup()
