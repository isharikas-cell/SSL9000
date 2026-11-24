#include "../Source/Shared/ConsoleLinkProtocol.h"
#include "../Source/Shared/BusSummingStage.h"
#include "../Source/Shared/WindowsSharedMemory.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <thread>
#if defined(_WIN32)
#include <windows.h>
#endif

using namespace ssl9000::link;

int main() {
#if defined(_WIN32)
    // Verify that two independent mapping handles see the same session bytes.
    {
        const auto uniqueName = std::wstring(L"Local\\\\SSL9000ConsoleLinkTest_")
            + std::to_wstring(GetCurrentProcessId()) + L"_"
            + std::to_wstring(GetTickCount64());
        WindowsSharedMemoryRegion creator;
        const auto created = creator.create(uniqueName, sizeof(SharedSessionMemory));
        assert(created == WindowsSharedMemoryRegion::CreateResult::created);
        assert(creator.size() == sizeof(SharedSessionMemory));

        auto* session = static_cast<SharedSessionMemory*>(creator.data());
        assert(initializeSharedSession(*session, 48000, 0x90001234u));

        WindowsSharedMemoryRegion opener;
        assert(opener.open(uniqueName, sizeof(SharedSessionMemory)));
        auto* openedSession = validateSharedSessionMapping(opener);
        assert(openedSession != nullptr);
        assert(openedSession->header.sessionNonce == 0x90001234u);
        assert(openedSession == session || opener.data() != nullptr);
    }
#endif

    // Session layout exposes fixed channel-to-bus and bus-to-master rings.
    // Allocate on the heap: the shared mapping is intentionally several MiB.
    {
        auto session = std::make_unique<SharedSessionMemory>();
        assert(!initializeSharedSession(*session, 0, 1234));
        assert(!isCompatibleSession(*session));
        assert(initializeSharedSession(*session, 48000, 1234));
        assert(isCompatibleSession(*session));
        assert(session->header.sampleRate == 48000);
        assert(session->header.sessionNonce == 1234);
        assert(session->header.byteSize == sizeof(SharedSessionMemory));

        SharedSpscAudioRing channelRing(session->channelToBus[7]);
        SharedSpscAudioRing masterRing(session->busToMaster[3]);
        std::array<float, 2> frame { 0.25f, -0.25f };
        std::array<float, 2> receivedFrame {};
        BlockHeader h {};
        h.sampleCount = 1;
        h.projectSamplePosition = 900;
        assert(channelRing.push(h, frame.data()));
        BlockHeader receivedHeader {};
        assert(channelRing.pop(receivedHeader, receivedFrame.data(), 1));
        assert(receivedHeader.projectSamplePosition == 900);
        assert(receivedFrame == frame);
        assert(masterRing.push(h, frame.data()));
        assert(masterRing.pop(receivedHeader, receivedFrame.data(), 1));
    }

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

    // Bus summing is linear and transparent when drive is disabled.
    {
        ssl9000::BusSummingStage bus;
        bus.prepare(48000.0);
        std::array<float, 4> l1 { 0.1f, 0.2f, -0.1f, 0.0f };
        std::array<float, 4> r1 { 0.0f, 0.1f, 0.2f, -0.1f };
        std::array<float, 4> l2 { 0.2f, -0.1f, 0.1f, 0.3f };
        std::array<float, 4> r2 { 0.1f, 0.0f, -0.1f, 0.2f };
        const float* left[] { l1.data(), l2.data() };
        const float* right[] { r1.data(), r2.data() };
        std::array<float, 4> outL {}, outR {};
        bus.process(left, right, 2, 4, outL.data(), outR.data(), 0.0f);
        for (std::size_t i = 0; i < 4; ++i) {
            assert(std::abs(outL[i] - (l1[i] + l2[i])) < 1.0e-6f);
            assert(std::abs(outR[i] - (r1[i] + r2[i])) < 1.0e-6f);
        }

        // With a hot summed signal, enabled bus drive reduces peaks smoothly.
        bus.reset();
        std::array<float, 64> hotL {}, hotR {}, drivenL {}, drivenR {};
        hotL.fill(0.8f);
        hotR.fill(0.8f);
        const float* hotLeft[] { hotL.data() };
        const float* hotRight[] { hotR.data() };
        bus.process(hotLeft, hotRight, 1, 64, drivenL.data(), drivenR.data(), 1.0f, -60.0f);
        assert(std::abs(drivenL.back()) < std::abs(hotL.back()));
        assert(std::abs(drivenR.back()) < std::abs(hotR.back()));

        // The fixed source limit is enforced without allocating or overrunning.
        std::array<const float*, 33> manyLeft {};
        std::array<const float*, 33> manyRight {};
        manyLeft.fill(l1.data());
        manyRight.fill(r1.data());
        bus.reset();
        bus.process(manyLeft.data(), manyRight.data(), 33, 4, outL.data(), outR.data(), 0.0f);
        for (std::size_t i = 0; i < 4; ++i) {
            assert(std::abs(outL[i] - 32.0f * l1[i]) < 1.0e-5f);
            assert(std::abs(outR[i] - 32.0f * r1[i]) < 1.0e-5f);
        }
    }

    return 0;
}
