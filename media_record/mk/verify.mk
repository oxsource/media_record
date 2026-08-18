# verify.mk — make verify entry point (task T032, spec 001 US3).
#
# Aggregates the full verification loop, mirroring native_ui / video_codec:
#   1. bazel build //...            (all targets incl. examples/tests)
#   2. bazel test //src/tests:all   (pipeline template validation + deps smoke)
#   3. bazel run hello_graph        (recorder.json end-to-end, exit 0)
#
# Each step must pass or make exits non-zero. A single entry point for CI.

BAZEL ?= bazel

.PHONY: verify clean

verify:
	@echo "==> [verify] (1/3) bazel build //..."
	@$(BAZEL) build //...
	@echo "==> [verify] (2/3) bazel test //src/tests:all"
	@$(BAZEL) test //src/tests:all
	@echo "==> [verify] (3/3) run //src/examples:hello_graph (recorder.json)"
	@$(BAZEL) run //src/examples:hello_graph -- --config=src/examples/configs/recorder.json
	@echo "[verify] ALL GREEN: build + tests + example"

clean:
	@$(BAZEL) clean 2>/dev/null || echo "[clean] no bazel workspace yet"
