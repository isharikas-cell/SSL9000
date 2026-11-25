#pragma once

#include "ConsoleLinkProtocol.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace ssl9000::link {

// Bounded, sample-position-aware consumer for one Bus Receive instance.
// Construct and reset outside the audio callback. process() performs no
// allocation, locking, waiting, or unbounded draining.
class BusAudioReceiver {
public:
    struct Diagnostics {
        std::uint64_t receivedBlocks = 0;
        std::uint64_t staleBlocks = 0;
        std::uint64_t wrongRouteBlocks = 0;
        std::uint64_t underrunBlocks = 0;
    };

    explicit BusAudioReceiver(SharedSessionMemory& session) noexcept
        : session(session) {}

    BusAudioReceiver(const BusAudioReceiver&) = delete;
    BusAudioReceiver& operator=(const BusAudioReceiver&) = delete;

    void reset() noexcept {
        for (auto& source : sources)
            source = SourceState {};
        diagnostics = {};
    }

    // Configure the fixed destination bus and the channel rings assigned to
    // this receiver. Call only off the audio thread. Each source ring must be
    // enabled for exactly one Bus Receive instance, preserving SPSC ownership.
    bool configure(std::uint32_t destinationBus,
                   const std::array<bool, maxChannels>& assignedSources) noexcept {
        if (destinationBus >= maxBuses)
            return false;
        reset();
        busId = destinationBus;
        enabledSources = assignedSources;
        configured = true;
        return true;
    }

    // Produces planar stereo for [projectSamplePosition, +sampleCount).
    // Missing source data is silence. A future/partial packet is retained and
    // consumed on a later call. Unassigned channel rings are never touched.
    bool process(std::int64_t projectSamplePosition,
                 std::uint32_t sampleCount,
                 float* outputLeft,
                 float* outputRight) noexcept {
        if (!configured || projectSamplePosition < 0
            || sampleCount == 0 || sampleCount > maxSamplesPerBlock
            || outputLeft == nullptr || outputRight == nullptr)
            return false;

        std::fill_n(outputLeft, sampleCount, 0.0f);
        std::fill_n(outputRight, sampleCount, 0.0f);
        const auto targetStart = static_cast<std::uint64_t>(projectSamplePosition);
        const auto targetEnd = targetStart + sampleCount;

        for (std::uint32_t sourceId = 0; sourceId < maxChannels; ++sourceId) {
            if (enabledSources[sourceId])
                consumeSource(sourceId, busId, targetStart, targetEnd,
                              outputLeft, outputRight);
        }

        return true;
    }

    const Diagnostics& getDiagnostics() const noexcept { return diagnostics; }

private:
    struct SourceState {
        bool pending = false;
        BlockHeader header {};
        std::uint32_t consumedSamples = 0;
        std::array<float, maxSamplesPerBlock * stereoChannels> samples {};
    };

    void consumeSource(std::uint32_t sourceId, std::uint32_t busId,
                       std::uint64_t targetStart, std::uint64_t targetEnd,
                       float* outputLeft, float* outputRight) noexcept {
        auto& source = sources[sourceId];
        SharedSpscAudioRing reader(session.channelToBus[sourceId]);

        // At most ringSlots stale/misrouted packets plus one usable packet
        // are examined per source per callback.
        for (std::uint32_t attempt = 0; attempt <= ringSlots; ++attempt) {
            if (!source.pending) {
                if (!reader.pop(source.header, source.samples.data(), maxSamplesPerBlock)) {
                    ++diagnostics.underrunBlocks;
                    return;
                }
                ++diagnostics.receivedBlocks;
                source.pending = true;
                source.consumedSamples = 0;

                if (source.header.sourceId != sourceId || source.header.busId != busId) {
                    source.pending = false;
                    ++diagnostics.wrongRouteBlocks;
                    continue;
                }
                if (source.header.projectSamplePosition < 0) {
                    source.pending = false;
                    ++diagnostics.staleBlocks;
                    continue;
                }
            }

            const auto blockStart = static_cast<std::uint64_t>(source.header.projectSamplePosition);
            const auto blockEnd = blockStart + source.header.sampleCount;
            const auto availableStart = blockStart + source.consumedSamples;

            if (blockEnd <= targetStart) {
                source.pending = false;
                ++diagnostics.staleBlocks;
                continue;
            }

            if (availableStart >= targetEnd)
                return; // Keep future audio for the matching callback.

            const auto overlapStart = std::max(availableStart, targetStart);
            const auto overlapEnd = std::min(blockEnd, targetEnd);
            const auto sourceOffset = source.consumedSamples
                + static_cast<std::uint32_t>(overlapStart - availableStart);
            const auto outputOffset = static_cast<std::uint32_t>(overlapStart - targetStart);
            const auto overlapCount = static_cast<std::uint32_t>(overlapEnd - overlapStart);

            for (std::uint32_t i = 0; i < overlapCount; ++i) {
                outputLeft[outputOffset + i] += source.samples[(sourceOffset + i) * 2];
                outputRight[outputOffset + i] += source.samples[(sourceOffset + i) * 2 + 1];
            }

            source.consumedSamples = sourceOffset + overlapCount;
            if (source.consumedSamples >= source.header.sampleCount) {
                source.pending = false;
                // Continue only when this block ended inside the requested
                // window; a following contiguous packet may fill its tail.
                if (blockEnd < targetEnd)
                    continue;
            }
            return;
        }
    }

    SharedSessionMemory& session;
    std::array<SourceState, maxChannels> sources {};
    std::array<bool, maxChannels> enabledSources {};
    std::uint32_t busId = 0;
    bool configured = false;
    Diagnostics diagnostics {};
};

} // namespace ssl9000::link
