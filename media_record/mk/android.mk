# android.mk — Android build / on-device verification module (spec 003).
#
# Mirrors video_codec's android module: cross-build dashcam_record for
# android_arm64 (MediaCodec surface backend), push binary + assets + android
# config to the device, run it, and pull the resulting MP4 to $(OUT_DIR).
#
# Targets (all prefixed `android-`):
#   android-build  — cross-compile only (CI gate)
#   android-push   — build + push binary/assets/config to the device
#   android-run    — build + push + run dashcam_record on the device (surface)
#   android-verify — build + push + run + pull + ffprobe/decode check
#
# Requires ANDROID_NDK_HOME for the cross-build and a connected device/emulator
# for push/run/verify.
#
# Optional parameters (forwarded to the on-device dashcam_record):
#   SURFACE=true|false  — force input_surface mode (default: android config)
#   SECONDS=N           — clip length in seconds (default 10, 30fps)

ANDROID_CONFIG := android_arm64

.PHONY: android-build android-push android-run android-verify

# Cross-build only (CI gate).
android-build:
	bash $(PWD)/scripts/verify/android_dashcam.sh build

# Build + push binary/assets/config to the device.
android-push:
	bash $(PWD)/scripts/verify/android_dashcam.sh push

# Build + push + run on the device (surface mode). Result stays on device.
android-run:
	bash $(PWD)/scripts/verify/android_dashcam.sh run $(if $(SECONDS),--seconds=$(SECONDS)) $(if $(SURFACE),--input-surface=$(SURFACE))

# Build + push + run + pull to $(OUT_DIR)/ + ffprobe/decode check.
android-verify:
	bash $(PWD)/scripts/verify/android_dashcam.sh verify $(if $(SECONDS),--seconds=$(SECONDS)) $(if $(SURFACE),--input-surface=$(SURFACE))
