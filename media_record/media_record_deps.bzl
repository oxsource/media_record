"""Dependency bootstrap for media_record.

References the three external capability repos by local path (no network fetch
of the repos themselves). Transitive http_archive deps of each repo are pulled
by calling its own setup function.

NOTE: Bazel forbids load() inside function bodies, so repo registration and the
external setup() calls are split across two files:
  - media_record_deps.bzl  : media_record_deps()  — registers the local_repository
  - media_record_setup.bzl : media_record_setup() — one-call transitive setup

WORKSPACE must call media_record_deps() BEFORE loading media_record_setup.bzl.

Machine-specific path overrides: edit MEDIA_RECORD_*_PATH below or use
--define flags via .user.bazelrc (see contracts/dependency-contract.md D-1~D-5).
"""

# local_repository is a native Bazel rule — no load() needed.

# Default local paths (inner WORKSPACE dirs of each repo — NOT the repo roots).
# Override per machine by editing these constants or via .user.bazelrc --define.
MEDIA_RECORD_GRAPH_RUNTIME_PATH = "/Users/moks/Develop/docker/ubuntu24/codes/graph_runtime/graph_runtime"
MEDIA_RECORD_NATIVE_UI_PATH = "/Users/moks/Develop/docker/ubuntu24/codes/native_ui/native_ui"
MEDIA_RECORD_VIDEO_CODEC_PATH = "/Users/moks/Develop/docker/ubuntu24/codes/video_codec/codec"


def media_record_deps():
    """Registers the three external repos via local_repository."""
    if not native.existing_rule("graph_runtime"):
        native.local_repository(
            name = "graph_runtime",
            path = MEDIA_RECORD_GRAPH_RUNTIME_PATH,
        )
    if not native.existing_rule("native_ui"):
        native.local_repository(
            name = "native_ui",
            path = MEDIA_RECORD_NATIVE_UI_PATH,
        )
    if not native.existing_rule("video_codec"):
        native.local_repository(
            name = "video_codec",
            path = MEDIA_RECORD_VIDEO_CODEC_PATH,
        )
