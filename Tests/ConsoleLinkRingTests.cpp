#include "../Source/Shared/ConsoleLinkProtocol.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <thread>

using namespace ssl9000::link;

int main() {
    SharedStreamRing shared {};
    SharedSpscAudioRing ring(shared);

    std::array<float, 16> input {};
    std::array<float, 16> output {};
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = static_cast<float>(i) * 0.125f;

    BlockHeader header {};
    header.projectSamplePosition = 4096;
    header.sampleCount = 8;
    header.channels = stereoChannels;

    assert(ring.push(header, input.data()));
    BlockHeader received {};
    assert(ring.pop(received, output.data(), 8));
    assert(received.sequence == 0);
    assert(received.projectSamplePosition == 4096);
    assert(received.sampleCount == 8);
    for (std::size_t i = 0; i < input.size(); ++i)
        assert(input[i] == output[i]);

    // Empty read and invalid-size write must fail without touching the ring.
    assert(!ring.pop(received, output.data(), 8));
    auto invalid = header;
    invalid.sampleCount = maxSamplesPerBlock + 1;
    assert(!ring.push(invalid, input.data()));

    // A full ring must reject new data instead of overwriting unread blocks.
    for (std::uint32_t i = 0; i < ringSlots; ++i)
        assert(ring.push(header, input.data()));
    assert(!ring.push(header, input.data()));
    for (std::uint32_t i = 0; i < ringSlots; ++i)
        assert(ring.pop(received, output.data(), 8));
    assert(!ring.pop(received, output.data(), 8));

    // Exercise concurrent single-producer/single-consumer operation and wraparound.
    constexpr std::uint64_t total = 20000;
    std::atomic<bool> failed { false };
    std::thread producer([&] {
        std::array<float, 2> frame {};
        BlockHeader h {};
        h.sampleCount = 1;
        for (std::uint64_t i = 0; i < total; ++i) {
            frame[0] = static_cast<float>(i);
            frame[1] = -static_cast<float>(i);
            h.projectSamplePosition = static_cast<std::int64_t>(i);
            while (!ring.push(h, frame.data()))
                std::this_thread::yield();
        }
    });
    std::thread consumer([&] {
        std::array<float, 2> frame {};
        BlockHeader h {};
        for (std::uint64_t i = 0; i < total; ++i) {
            while (!ring.pop(h, frame.data(), 1))
                std::this_thread::yield();
            if (h.projectSamplePosition != static_cast<std::int64_t>(i)
                || frame[0] != static_cast<float>(i)
                || frame[1] != -static_cast<float>(i))
                failed.store(true, std::memory_order_relaxed);
        }
    });
    producer.join();
    consumer.join();
    assert(!failed.load(std::memory_order_relaxed));
    return 0;
}
