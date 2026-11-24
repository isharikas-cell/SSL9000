#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace ssl9000::link {

// Shared-memory ABI. Bump protocolVersion whenever any field/layout changes.
// Create and initialize the mapping before audio processing starts.
inline constexpr std::uint32_t protocolVersion = 1;
inline constexpr std::uint32_t maxChannels = 32;
inline constexpr std::uint32_t maxBuses = 32;
inline constexpr std::uint32_t maxSamplesPerBlock = 2048;
inline constexpr std::uint32_t ringSlots = 8;
inline constexpr std::uint32_t stereoChannels = 2;

struct StreamIdentity {
    std::uint32_t busId = 0;
    std::uint32_t sourceId = 0;
};

struct BlockHeader {
    std::uint64_t sequence = 0;
    std::int64_t projectSamplePosition = 0;
    std::uint32_t sampleCount = 0;
    std::uint32_t channels = stereoChannels;
    std::uint32_t flags = 0;
    std::uint32_t reserved = 0;
};

struct alignas(64) SharedAudioSlot {
    // 0 = free; otherwise the exact producer sequence + 1 published in this slot.
    std::atomic<std::uint64_t> state { 0 };
    BlockHeader header {};
    std::array<float, maxSamplesPerBlock * stereoChannels> samples {};
};

struct alignas(64) SharedStreamRing {
    std::atomic<std::uint64_t> writeSequence { 0 };
    std::atomic<std::uint64_t> readSequence { 0 };
    std::array<SharedAudioSlot, ringSlots> slots {};
};

struct SharedSessionHeader {
    std::uint32_t version = protocolVersion;
    std::uint32_t byteSize = 0;
    std::uint32_t sampleRate = 0;
    std::uint32_t maxBlockSize = maxSamplesPerBlock;
    std::uint64_t sessionNonce = 0;
    std::atomic<std::uint32_t> active { 0 };
};

// Single-producer/single-consumer bounded ring. push/pop never allocate or wait.
// The mapping creator must zero-initialize the complete SharedStreamRing before
// publishing the session as active. One ring must never have multiple writers
// or multiple readers.
class SharedSpscAudioRing {
public:
    explicit SharedSpscAudioRing(SharedStreamRing& shared) noexcept : ring(shared) {}

    bool push(const BlockHeader& header, const float* interleavedStereo) noexcept {
        if (interleavedStereo == nullptr || header.channels != stereoChannels
            || header.sampleCount == 0 || header.sampleCount > maxSamplesPerBlock)
            return false;

        const auto sequence = ring.writeSequence.load(std::memory_order_relaxed);
        auto& slot = ring.slots[sequence % ringSlots];
        if (slot.state.load(std::memory_order_acquire) != 0)
            return false; // Full: never overwrite unread audio.

        slot.header = header;
        slot.header.sequence = sequence;
        const auto sampleValues = static_cast<std::size_t>(header.sampleCount) * stereoChannels;
        std::memcpy(slot.samples.data(), interleavedStereo, sampleValues * sizeof(float));

        slot.state.store(sequence + 1, std::memory_order_release);
        ring.writeSequence.store(sequence + 1, std::memory_order_relaxed);
        return true;
    }

    bool pop(BlockHeader& header, float* interleavedStereo,
             std::uint32_t outputCapacitySamples) noexcept {
        if (interleavedStereo == nullptr)
            return false;

        const auto sequence = ring.readSequence.load(std::memory_order_relaxed);
        auto& slot = ring.slots[sequence % ringSlots];
        if (slot.state.load(std::memory_order_acquire) != sequence + 1)
            return false; // Empty or not yet published.

        const auto count = slot.header.sampleCount;
        if (slot.header.channels != stereoChannels || count == 0
            || count > maxSamplesPerBlock || count > outputCapacitySamples) {
            // Do not consume a malformed/oversized block into a smaller buffer.
            return false;
        }

        header = slot.header;
        const auto sampleValues = static_cast<std::size_t>(count) * stereoChannels;
        std::memcpy(interleavedStereo, slot.samples.data(), sampleValues * sizeof(float));

        slot.state.store(0, std::memory_order_release);
        ring.readSequence.store(sequence + 1, std::memory_order_relaxed);
        return true;
    }

private:
    SharedStreamRing& ring;
};

static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "Console Link requires lock-free 64-bit atomics");
static_assert(std::is_standard_layout_v<BlockHeader>);
static_assert(sizeof(float) == 4);
} // namespace ssl9000::link
