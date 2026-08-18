# media_record build/verify mechanism — top-level entry point.
#
# Modules live in mk/ and are self-describing (AOSP-style, cf. native_ui/mk).
# Canonical targets are namespaced per module; friendly aliases live here.

SHELL := /bin/bash

include mk/verify.mk

.DEFAULT_GOAL := help

.PHONY: help

help:
	@echo "media_record targets:"
	@echo "  make verify    Run build + tests + example verification"
	@echo "  make clean     Clean bazel outputs"