#ifndef MEDIA_RECORD_NODES_SIGNAL_SOURCE_SIGNAL_EVENT_H_
#define MEDIA_RECORD_NODES_SIGNAL_SOURCE_SIGNAL_EVENT_H_

#include <cstdint>

// Bypass signal event (spec 002, data-model.md §3): minimal event produced by
// SignalSourceNode and consumed (optionally) by UiOverlayNode. This feature
// renders only the timestamp OSD, so consumers may ignore the events — the
// type stays shared so packets are type-checked across nodes.

namespace media::record {

struct SignalEvent {
  enum class Type { kNone, kTick };
  Type type = Type::kTick;
  int64_t timestamp_us = 0;
};

}  // namespace media::record

#endif  // MEDIA_RECORD_NODES_SIGNAL_SOURCE_SIGNAL_EVENT_H_
