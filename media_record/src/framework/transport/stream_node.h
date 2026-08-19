#ifndef MEDIA_RECORD_FRAMEWORK_STREAM_STREAM_NODE_H_
#define MEDIA_RECORD_FRAMEWORK_STREAM_STREAM_NODE_H_

#include <map>
#include <string>

#include "media_record/node.h"
#include "stream_buffer.h"

// Data-flow node interface (spec 002).
//
// Extends the skeleton media::record::Node (Open/Process/Close) with per-stream
// I/O access: the PipelineRunner injects the node's declared input/output
// StreamBuffers (keyed by stream name) before Open(). Concrete nodes read their
// inputs and write their outputs through Input()/Output() inside Process().
// Nodes that are not StreamNodes (e.g. 001 skeleton stubs) still run: the
// runner drives them via the base Node contract without data wiring.

namespace media::record {

class StreamNode : public Node {
 public:
  // Runner wiring: maps stream_name -> buffer for every stream the node
  // declares in its config (input_streams + output_streams). Called once,
  // before Open(). Pointers are owned by the runner and stay valid for the
  // node's lifetime.
  void AttachStreams(const std::map<std::string, StreamBuffer*>& streams) {
    streams_ = streams;
  }

 protected:
  // The buffer for |stream_name|, or nullptr when the stream was not wired.
  StreamBuffer* Input(const std::string& stream_name) const {
    return Buffer(stream_name);
  }
  StreamBuffer* Output(const std::string& stream_name) const {
    return Buffer(stream_name);
  }

 private:
  StreamBuffer* Buffer(const std::string& stream_name) const {
    auto it = streams_.find(stream_name);
    return it != streams_.end() ? it->second : nullptr;
  }

  std::map<std::string, StreamBuffer*> streams_;
};

}  // namespace media::record

#endif  // MEDIA_RECORD_FRAMEWORK_STREAM_STREAM_NODE_H_
