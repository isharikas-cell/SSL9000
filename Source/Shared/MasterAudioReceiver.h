#pragma once

#include "ConsoleLinkProtocol.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace ssl9000::link {

// Bounded, sample-position-aware consumer for the completed bus streams.
// Each selected bus ring has exactly one Master reader.
class MasterAudioReceiver {
public:
    struct Diagnostics {
        std::uint64_t receivedBlocks = 0;
        std::uint64_t staleBlocks = 0;
        std::uint64_t invalidBlocks = 0;
        std::uint64_t underrunBlocks = 0;
    };

    explicit MasterAudioReceiver(SharedSessionMemory& shared) noexcept : session(shared) {}
    MasterAudioReceiver(const MasterAudioReceiver&) = delete;
    MasterAudioReceiver& operator=(const MasterAudioReceiver&) = delete;

    void reset() noexcept {
        for (auto& bus : buses)
            bus = BusState {};
        diagnostics = {};
    }

    bool configure(const std::array<bool, maxBuses>& selected) noexcept {
        reset();
        enabled = selected;
        configured = true;
        return true;
    }

    bool process(std::int64_t samplePosition, std::uint32_t count,
                 float* left, float* right) noexcept {
        if (!configured || samplePosition < 0 || count == 0
            || count > maxSamplesPerBlock || left == nullptr || right == nullptr)
            return false;
        std::fill_n(left, count, 0.0f);
        std::fill_n(right, count, 0.0f);
        const auto start = static_cast<std::uint64_t>(samplePosition);
        const auto end = start + count;
        for (std::uint32_t id = 0; id < maxBuses; ++id)
            if (enabled[id])
                consume(id, start, end, left, right);
        return true;
    }

    const Diagnostics& getDiagnostics() const noexcept { return diagnostics; }

private:
    struct BusState {
        bool pending = false;
        BlockHeader header {};
        std::uint32_t consumed = 0;
        std::array<float, maxSamplesPerBlock * stereoChannels> samples {};
    };

    void consume(std::uint32_t id, std::uint64_t start, std::uint64_t end,
                 float* left, float* right) noexcept {
        auto& state = buses[id];
        SharedSpscAudioRing reader(session.busToMaster[id]);
        for (std::uint32_t attempt = 0; attempt <= ringSlots; ++attempt) {
            if (!state.pending) {
                if (!reader.pop(state.header, state.samples.data(), maxSamplesPerBlock)) {
                    ++diagnostics.underrunBlocks;
                    return;
                }
                ++diagnostics.receivedBlocks;
                state.pending = true;
                state.consumed = 0;
                if (state.header.busId != id || state.header.sourceId != id
                    || state.header.projectSamplePosition < 0) {
                    state.pending = false;
                    ++diagnostics.invalidBlocks;
                    continue;
                }
            }
            const auto blockStart = static_cast<std::uint64_t>(state.header.projectSamplePosition);
            const auto blockEnd = blockStart + state.header.sampleCount;
            const auto availableStart = blockStart + state.consumed;
            if (blockEnd <= start) {
                state.pending = false;
                ++diagnostics.staleBlocks;
                continue;
            }
            if (availableStart >= end)
                return;
            const auto overlapStart = std::max(availableStart, start);
            const auto overlapEnd = std::min(blockEnd, end);
            const auto sourceOffset = state.consumed
                + static_cast<std::uint32_t>(overlapStart - availableStart);
            const auto outputOffset = static_cast<std::uint32_t>(overlapStart - start);
            const auto overlapCount = static_cast<std::uint32_t>(overlapEnd - overlapStart);
            for (std::uint32_t i = 0; i < overlapCount; ++i) {
                left[outputOffset + i] += state.samples[(sourceOffset + i) * 2];
                right[outputOffset + i] += state.samples[(sourceOffset + i) * 2 + 1];
            }
            state.consumed = sourceOffset + overlapCount;
            if (state.consumed >= state.header.sampleCount) {
                state.pending = false;
                if (blockEnd < end)
                    continue;
            }
            return;
        }
    }

    SharedSessionMemory& session;
    std::array<BusState, maxBuses> buses {};
    std::array<bool, maxBuses> enabled {};
    bool configured = false;
    Diagnostics diagnostics {};
};

} // namespace ssl9000::link
