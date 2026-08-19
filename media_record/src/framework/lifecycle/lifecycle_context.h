// lifecycle_context.h
//
// Cross-node, graph-lifetime shared state for the dashcam pipeline. The runner
// (dashcam_record.cc) owns a LifecycleContext and injects a POINTER to it via
// SetInputSidePacket(LifecycleContext::kSidePacketTag,
//                    Packet::MakePacket<LifecycleContext*>(&ctx)).
//
// Pointer (not value) injection is the key: the pointer is valid before graph
// Open, while its FIELDS can be written/read at runtime across the Open/Process
// boundary — so a node that Open()s first (DashcamRenderNode) can lazily read a
// field that a later-Opened node (VideoEncoderNode) wrote, with no Open-order
// race (the scheduler guarantees all Open()s complete before any source
// Process() runs). See specs/003 contracts §4.2 (D.2/D.3).

#pragma once

#include <cstdint>

namespace media {
namespace record {

// Graph-lifetime shared state, owned by the runner. Nodes only read/write
// fields (they never own or free the struct).
struct LifecycleContext {
  // Side-packet tag under which the runner injects this pointer
  // (SetInputSidePacket(kSidePacketTag, ...)); nodes read it back via
  // ctx.InputSidePackets().Get(LifecycleContext::kSidePacketTag). Single
  // source of truth so all users share one tag string.
  static constexpr const char* kSidePacketTag = "lifecycle_ctx";

  bool pipeline_failed = false;   // first-error abort flag (FR-009)
  // Android/surface mode: the VideoEncoderNode's CreateInputSurface() result
  // (ANativeWindow* as void*), written in its Open(). DashcamRenderNode reads
  // it on first Process() to host its RenderContext on the encoder's input
  // surface. null on host / non-surface mode.
  void* input_surface = nullptr;
};

// Generic lightweight cross-node notification carried on a graph stream.
// A producer emits a PacketNotify instead of (or as an event marker alongside)
// a heavy data payload — e.g. in Android/surface mode the render node draws
// directly onto the encoder's input surface (GPU, no CPU VideoFrame) and sends
// a PacketNotify so the encoder can Poll() the hardware encoder. Reusable by
// any source/event-driven node that only needs to signal "something happened /
// data is ready" without shipping pixel buffers.
struct PacketNotify {
  int64_t timestamp_us = 0;  // event timestamp (presentation-time or wall clock)
};

}  // namespace record
}  // namespace media
