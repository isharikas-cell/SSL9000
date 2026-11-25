#pragma once

#include "ConsoleLinkProtocol.h"

namespace ssl9000::link {

// Real-time-safe publisher endpoint for one Channel instance. The endpoint is
// bound to one channel ring and one destination bus. Session discovery and
// mapping ownership intentionally remain outside this class.
class ChannelAudioPublisher {
public:
    ChannelAudioPublisher(SharedStreamRing& ring, std::uint32_t sourceId,
                          std::uint32_t busId) noexcept
        : ring(ring), sourceId(sourceId), busId(busId) {}

    bool isConfigured() const noexcept {
        return sourceId < maxChannels && busId < maxBuses;
    }

    bool publish(std::int64_t projectSamplePosition, std::uint32_t sampleCount,
                 const float* interleavedStereo, std::uint32_t flags = 0) noexcept {
        if (!isConfigured())
            return false;
        BlockHeader header {};
        header.projectSamplePosition = projectSamplePosition;
        header.sampleCount = sampleCount;
        header.channels = stereoChannels;
        header.flags = flags;
        header.busId = busId;
        header.sourceId = sourceId;
        return writer.push(header, interleavedStereo);
    }

private:
    SharedStreamRing& ring;
    const std::uint32_t sourceId;
    const std::uint32_t busId;
    SharedSpscAudioRing writer { ring };
};

} // namespace ssl9000::link
