#ifndef MEDIA_RECORD_FRAMEWORK_STREAM_STREAM_BUFFER_H_
#define MEDIA_RECORD_FRAMEWORK_STREAM_STREAM_BUFFER_H_

// Per-stream bounded mailbox (spec 002). Scaffold only: the real
// single-consumer stream buffer (write/read, capacity limit, EOS flag) lands
// in task T006.

namespace media::record {

class StreamBuffer;  // TODO(T006): implement per-stream bounded mailbox

}  // namespace media::record

#endif  // MEDIA_RECORD_FRAMEWORK_STREAM_STREAM_BUFFER_H_
