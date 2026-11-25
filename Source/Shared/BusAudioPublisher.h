#pragma once

#include "ConsoleLinkProtocol.h"

#include <cstdint>

namespace ssl9000::link {

// Single producer for one completed bus stream. Construct off the audio thread.
class BusAudioPublisher {
public:
    BusAudioPublisher(SharedStreamRing& ring, std::uint32_t busId) noexcept
        : writer(ring), busId(busId), configured(busId < maxBuses) {}

    bool isConfigured() const noexcept { return configured; }

    bool publish(std::int64_t projectSamplePosition, std::uint32_t sampleCount,
                 const float* interleavedStereo) noexcept {
        if (!configured || projectSamplePosition < 0 || sampleCount == 0
            || sampleCount > maxSamplesPerBlock)
            return false;
        BlockHeader header {};
        header.projectSamplePosition = projectSamplePosition;
        header.sampleCount = sampleCount;
        header.channels = stereoChannels;
        header.busId = busId;
        header.sourceId = busId;
        return writer.push(header, interleavedStereo);
    }

private:
    SharedSpscAudioRing writer;
    std::uint32_t busId = 0;
    bool configured = false;
};

} // namespace ssl9000::link
