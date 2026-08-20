#!/usr/bin/env bash
# Android cross-build + on-device verification of the dashcam pipeline
# (spec 003: MediaCodec surface backend). Mirrors video_codec's android_codec.sh.
#
# Modes:
#   build   — cross-compile only (CI gate)
#   push    — build + push the binary, images (road/dog) and the android config
#             to the device
#   run     — build + push + run dashcam_record on the device (surface mode,
#             Android default config) producing out/dashcam.mp4
#   verify  — build + push + run + pull the MP4 and ffprobe/decode-check it
#             (default)
#
# Requires ANDROID_NDK_HOME for the cross-build and a connected device/emulator
# for push/run/verify.
#
# Usage:
#   android_dashcam.sh [build|push|run|verify] [seconds] [--input-surface=true|false]
#   --input-surface overrides the render/encoder dataflow mode (true = MediaCodec
#   input surface, false = CPU memory path) on top of the android config.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

MODE="verify"
CLIP_SECONDS=10
INPUT_SURFACE=""

for arg in "$@"; do
    case "${arg}" in
        build|push|run|verify)
            MODE="${arg}"
            ;;
        --seconds=*)
            CLIP_SECONDS="${arg#*=}"
            ;;
        --input-surface=true|--input-surface=1)
            INPUT_SURFACE="--input-surface=true"
            ;;
        --input-surface=false|--input-surface=0)
            INPUT_SURFACE="--input-surface=false"
            ;;
        --help|-h)
            echo "usage: android_dashcam.sh [build|push|run|verify] [--seconds=N] [--input-surface=true|false]"
            exit 0
            ;;
        *)
            echo "[android] error: unknown argument '${arg}'" >&2
            exit 2
            ;;
    esac
done

DEV_DIR="/data/local/tmp/media_record"

echo "[android] build //src/examples:dashcam_record (--config android_arm64)"
bazel build //src/examples:dashcam_record --config android_arm64
if [[ "${MODE}" == "build" ]]; then
    echo "[android] build OK"
    exit 0
fi

BIN="$(find -L bazel-bin -path '*/examples/dashcam_record' -type f | head -1)"
[ -n "${BIN}" ] || { echo "[android] FAIL: dashcam_record binary not found"; exit 1; }

DEVICE="$(adb devices | awk 'NR>1 && $2=="device" {print $1; exit}')"
[ -n "${DEVICE}" ] || {
    echo "[android] FAIL: no adb device/emulator connected (start one, then retry)"
    exit 1
}

# --- push resources ---------------------------------------------------------
# dashcam_record runs from $DEV_DIR with config/asset paths relative to it:
#   config   -> src/examples/configs/dashcam_record_android.json (surface mode)
#   assets   -> src/examples/assets/{dashcam_road.png,flydog.png}
echo "[android] push binary + assets + config to ${DEVICE}:${DEV_DIR}"
adb -s "${DEVICE}" shell "mkdir -p ${DEV_DIR}/src/examples/assets ${DEV_DIR}/src/examples/configs ${DEV_DIR}/out"
adb -s "${DEVICE}" push "${BIN}" "${DEV_DIR}/dashcam_record" >/dev/null
adb -s "${DEVICE}" push "src/examples/assets/dashcam_road.png" "${DEV_DIR}/src/examples/assets/dashcam_road.png" >/dev/null
adb -s "${DEVICE}" push "src/examples/assets/flydog.png" "${DEV_DIR}/src/examples/assets/flydog.png" >/dev/null
adb -s "${DEVICE}" push "src/examples/configs/dashcam_record_android.json" "${DEV_DIR}/src/examples/configs/dashcam_record_android.json" >/dev/null
adb -s "${DEVICE}" shell "chmod +x ${DEV_DIR}/dashcam_record"

if [[ "${MODE}" == "push" ]]; then
    echo "[android] push OK"
    exit 0
fi

# --- run --------------------------------------------------------------------
# The android config's asset/output paths target the /data/local test layout
# (${DEV_DIR}). frame_count = 300 (10s @ 30fps); --seconds=N scales it (fps=30).
MODE_LABEL="surface mode"
RUN_ARGS="--config=src/examples/configs/dashcam_record_android.json"
RUN_ARGS="${RUN_ARGS} --frames=$((CLIP_SECONDS * 30))"
if [[ -n "${INPUT_SURFACE}" ]]; then
    RUN_ARGS="${RUN_ARGS} ${INPUT_SURFACE}"
    if [[ "${INPUT_SURFACE}" == "--input-surface=false" ]]; then
        MODE_LABEL="CPU memory mode"
    fi
fi
echo "[android] run dashcam_record on ${DEVICE} (${CLIP_SECONDS}s, ${MODE_LABEL})"
adb -s "${DEVICE}" shell "cd ${DEV_DIR} && ./dashcam_record ${RUN_ARGS}"

if [[ "${MODE}" == "run" ]]; then
    echo "[android] run OK (result on device: ${DEV_DIR}/out/dashcam.mp4)"
    exit 0
fi

# --- verify: pull + ffprobe/decode check -----------------------------------
mkdir -p "${ROOT}/out"
adb -s "${DEVICE}" pull "${DEV_DIR}/out/dashcam.mp4" "${ROOT}/out/dashcam.mp4" >/dev/null

OUT="${ROOT}/out/dashcam.mp4"
SIZE="$(wc -c < "$OUT" | tr -d ' ')"
[ "$SIZE" -gt 0 ] || { echo "[android] FAIL: out/dashcam.mp4 is empty"; exit 1; }

INFO="$(ffprobe -v error -show_entries stream=codec_name,codec_type,width,height \
        -of default=noprint_wrappers=1 "$OUT")"
FMT="$(ffprobe -v error -show_entries format=format_name \
        -of default=noprint_wrappers=1 "$OUT")"
echo "[android] pulled $OUT ($SIZE bytes)"
echo "$INFO"

echo "$INFO" | grep -q 'codec_name=h264' || { echo '[android] FAIL: no h264 video stream'; exit 1; }
echo "$FMT"  | grep -q 'mp4'             || { echo "[android] FAIL: not an mp4 container ('$FMT')"; exit 1; }

# Decode pass: catch malformed sample framing.
DECODE_ERR="$(ffmpeg -v error -i "$OUT" -f null - 2>&1 >/dev/null)"
if [[ -n "${DECODE_ERR}" ]]; then
    echo "[android] FAIL: decode errors:"
    echo "${DECODE_ERR}"
    exit 1
fi

echo "[android] PASS: valid H.264 MP4 via MediaCodec surface backend ($SIZE bytes)"
