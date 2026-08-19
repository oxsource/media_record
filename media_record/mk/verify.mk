# verify.mk — make verify entry point (spec 001 US3; extended by spec 002 T028).
#
# Aggregates the full verification loop, mirroring native_ui / video_codec:
#   1. bazel build //...             (all targets incl. examples/tests)
#   2. bazel test //src/tests:all    (template validation + unit + e2e tests)
#   3. bazel run dashcam_record      (default 10s recording -> out/dashcam.mp4)
#   4. recording artifact check      (out/dashcam.mp4 exists, non-empty, MP4)
#
# Each step must pass or make exits non-zero. A single entry point for CI.

BAZEL ?= bazel

.PHONY: verify clean

verify:
	@echo "==> [verify] (1/4) bazel build //..."
	@$(BAZEL) build //...
	@echo "==> [verify] (2/4) bazel test //src/tests:all"
	@$(BAZEL) test //src/tests:all
	@echo "==> [verify] (3/4) run //src/examples:dashcam_record (default 10s recording)"
	@$(BAZEL) run //src/examples:dashcam_record
	@echo "==> [verify] (4/4) recording artifact check: out/dashcam.mp4"
	@test -s out/dashcam.mp4 || { echo "[verify] FAIL: out/dashcam.mp4 missing or empty"; exit 1; }
	@head -c 8 out/dashcam.mp4 | grep -q "ftyp" || { echo "[verify] FAIL: out/dashcam.mp4 is not an MP4"; exit 1; }
	@echo "[verify] ALL GREEN: build + tests + recording artifact"

clean:
	@$(BAZEL) clean 2>/dev/null || echo "[clean] no bazel workspace yet"
