#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>

namespace ssl9000::link {

// Shared-memory ABI. Bump protocolVersion whenever any field/layout changes.
// Fixed-capacity slots keep the real-time path allocation-free.
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
    // State protocol: 0 = free, odd = producer owns slot, even non-zero = ready.
    // The final release-store publishes the complete header and payload.
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

static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "Console Link requires lock-free 64-bit atomics");
static_assert(std::is_standard_layout_v<BlockHeader>);
static_assert(maxSamplesPerBlock >= 2048);

// This header intentionally defines only the shared-memory ABI. OS mapping,
// session discovery, producer/consumer scheduling, and lifecycle management
// are implemented by the transport backend, not by audio callbacks.
} // namespace ssl9000::link
