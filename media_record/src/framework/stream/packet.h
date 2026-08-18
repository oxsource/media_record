#ifndef MEDIA_RECORD_FRAMEWORK_STREAM_PACKET_H_
#define MEDIA_RECORD_FRAMEWORK_STREAM_PACKET_H_

// Frame-transport packet (spec 002). Scaffold only: the real implementation
// (variant payload of video::codec::VideoFrame / video::codec::VideoPacket /
// SignalEvent plus stream_name and pts_us) lands in task T005.

namespace media::record {

class Packet;  // TODO(T005): implement frame-transport packet

}  // namespace media::record

#endif  // MEDIA_RECORD_FRAMEWORK_STREAM_PACKET_H_
