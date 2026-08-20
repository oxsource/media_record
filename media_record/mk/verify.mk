# verify.mk — make verify entry point (spec 001 US3; extended by spec 002 T028).
#
# Aggregates the full verification loop, mirroring native_ui / video_codec:
#   1. bazel build //...             (all targets incl. examples/tests)
#   2. bazel test //src/tests:all    (template validation + unit + e2e tests)
#   3. render-demo                   (DashcamRenderer standalone -> out/*.png)
#   4. run dashcam_record            (default 10s recording -> out/dashcam.mp4)
#   5. recording artifact check      (out/dashcam.mp4 exists, non-empty, MP4)
#
# Each step must pass or make exits non-zero. A single entry point for CI.

BAZEL ?= bazel
OUT_DIR ?= out

.PHONY: verify clean clean-out render-demo

# One-click standalone DashcamRenderer debug (shared with the top-level Makefile
# target of the same name). --out uses an absolute path so PNGs land in
# <repo>/$(OUT_DIR)/ regardless of bazel run's runfiles cwd.
render-demo:
	@mkdir -p $(OUT_DIR)
	@$(BAZEL) run //src/examples:render_demo -- --out=$(abspath $(OUT_DIR))

verify:
	@echo "==> [verify] (1/5) bazel build //..."
	@$(BAZEL) build //...
	@echo "==> [verify] (2/5) bazel test //src/tests:all"
	@$(BAZEL) test //src/tests:all
	@echo "==> [verify] (3/5) run //src/examples:render_demo (PNG -> $(OUT_DIR)/)"
	@$(MAKE) render-demo
	@test -s $(OUT_DIR)/render_demo_000.png || { echo "[verify] FAIL: render-demo PNG missing"; exit 1; }
	@echo "==> [verify] (4/5) run //src/examples:dashcam_record (default 10s recording)"
	@$(BAZEL) run //src/examples:dashcam_record
	@echo "==> [verify] (5/5) recording artifact check: out/dashcam_host_cpu.mp4"
	@test -s out/dashcam_host_cpu.mp4 || { echo "[verify] FAIL: out/dashcam_host_cpu.mp4 missing or empty"; exit 1; }
	@head -c 8 out/dashcam_host_cpu.mp4 | grep -q "ftyp" || { echo "[verify] FAIL: out/dashcam_host_cpu.mp4 is not an MP4"; exit 1; }
	@echo "[verify] ALL GREEN: build + tests + render + recording artifact"

clean:
	@$(BAZEL) clean 2>/dev/null || echo "[clean] no bazel workspace yet"

# Remove generated artifacts under $(OUT_DIR) (render PNGs, dashcam.mp4, ...).
# Keeps the directory itself (render-demo recreates it anyway).
clean-out:
	@if [ -d "$(OUT_DIR)" ]; then \
	  rm -rf "$(OUT_DIR)"/* "$(OUT_DIR)"/.[!.]* 2>/dev/null; \
	  echo "[clean-out] removed $(OUT_DIR)/ contents"; \
	else \
	  echo "[clean-out] $(OUT_DIR)/ does not exist, nothing to remove"; \
	fi
