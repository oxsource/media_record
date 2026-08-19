#ifndef MEDIA_RECORD_FRAMEWORK_STREAM_STREAM_BUFFER_H_
#define MEDIA_RECORD_FRAMEWORK_STREAM_STREAM_BUFFER_H_

#include <cstddef>
#include <deque>
#include <string>

#include "packet.h"

// Per-stream bounded mailbox (spec 002 / contracts/pipeline-contract.md P-4).
//
// One buffer per stream name. Producers Push() packets; a single consumer
// Pop()s them in FIFO order (single-consumer semantics). MarkEos() records that
// the stream is ending — it is a signal TO CONSUMERS (so they can flush /
// finalize, e.g. encoder drain, muxer trailer), NOT a write barrier: producers
// may still Push() their final flush packets after EOS (the runner marks every
// buffer EOS before the drain pass). The bounded capacity keeps a misbehaving
// producer from growing memory without bound.

namespace media::record {

class StreamBuffer {
 public:
  explicit StreamBuffer(std::string stream_name, size_t capacity = 100)
      : stream_name_(std::move(stream_name)), capacity_(capacity) {}

  const std::string& stream_name() const { return stream_name_; }

  // Appends |packet|. Returns false only when the buffer is at capacity.
  // EOS does not block writes (producers emit final flush packets after EOS).
  bool Push(Packet packet) {
    if (queue_.size() >= capacity_) return false;
    queue_.push_back(std::move(packet));
    return true;
  }

  // Removes the next packet into |out|. Returns false when empty.
  bool Pop(Packet* out) {
    if (queue_.empty()) return false;
    if (out) *out = std::move(queue_.front());
    queue_.pop_front();
    return true;
  }

  void MarkEos() { eos_ = true; }
  bool eos() const { return eos_; }
  bool Empty() const { return queue_.empty(); }
  size_t Size() const { return queue_.size(); }
  size_t Capacity() const { return capacity_; }

 private:
  std::string stream_name_;
  size_t capacity_;
  std::deque<Packet> queue_;
  bool eos_ = false;
};

}  // namespace media::record

#endif  // MEDIA_RECORD_FRAMEWORK_STREAM_STREAM_BUFFER_H_
