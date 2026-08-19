# media_record build/verify mechanism — top-level entry point.
#
# Modules live in mk/ and are self-describing (AOSP-style, cf. native_ui/mk).
# Canonical targets are namespaced per module; friendly aliases live here.

SHELL := /bin/bash

include mk/verify.mk

.DEFAULT_GOAL := help

# Output directory for one-off examples (relative to the repo root).
OUT_DIR := out

.PHONY: help

help:
	@echo "media_record targets:"
	@echo "  make verify       Run build + tests + example verification"
	@echo "  make render-demo  Build & run the standalone render demo"
	@echo "                    (writes PNG frames to $(OUT_DIR)/)"
	@echo "  make clean        Clean bazel outputs"
	@echo "  make clean-out    Remove generated artifacts under $(OUT_DIR)/"