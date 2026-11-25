#include "../Source/Shared/ConsoleLinkProtocol.h"
#include "../Source/Shared/ChannelAudioPublisher.h"
#include "../Source/Shared/BusSummingStage.h"
#include "../Source/Shared/BusAudioReceiver.h"
#include "../Source/Shared/WindowsSharedMemory.h"
#include "../Source/Shared/WindowsConsoleLinkSession.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <memory>
#include <thread>
#include <string>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#endif

using namespace ssl9000::link;

#if defined(_WIN32)
int wmain(int argc, wchar_t* argv[]) {
    if (argc == 3 && std::wstring(argv[1]) == L"--consumer") {
        WindowsSharedMemoryRegion region;
        if (!region.open(argv[2], sizeof(SharedSessionMemory)))
            return 10;
        auto* session = validateSharedSessionMapping(region);
        if (session == nullptr || session->header.sessionNonce != 0x90005678u)
            return 11;

        SharedSpscAudioRing receiver(session->channelToBus[5]);
        std::array<float, 4> samples {};
        BlockHeader header {};
        const auto deadline = GetTickCount64() + 10000;
        while (GetTickCount64() < deadline) {
            if (receiver.pop(header, samples.data(), 2)) {
                return header.projectSamplePosition == 1234567
                    && header.sampleCount == 2
                    && samples[0] == 0.125f && samples[1] == -0.125f
                    && samples[2] == 0.25f && samples[3] == -0.25f ? 0 : 12;
            }
            Sleep(1);
        }
        return 13;
    }
#else
int main() {
#endif
#if defined(_WIN32)
    // Verify that two independent mapping handles see the same session bytes.
    {
        const auto uniqueName = std::wstring(L"Local\\SSL9000ConsoleLinkTest_")
            + std::to_wstring(GetCurrentProcessId()) + L"_"
            + std::to_wstring(GetTickCount64());
        WindowsSharedMemoryRegion creator;
        const auto created = creator.create(uniqueName, sizeof(SharedSessionMemory));
        assert(created == WindowsSharedMemoryRegion::CreateResult::created);
        assert(creator.size() == sizeof(SharedSessionMemory));

        auto* session = initializeNewSharedSessionMapping(creator, 48000, 0x90001234u);
        assert(session != nullptr);

        WindowsSharedMemoryRegion opener;
        assert(opener.open(uniqueName, sizeof(SharedSessionMemory)));
        auto* openedSession = validateSharedSessionMapping(opener);
        assert(openedSession != nullptr);
        assert(openedSession->header.sessionNonce == 0x90001234u);
        assert(openedSession->header.sampleRate == 48000);
    }

    // Project-keyed session discovery is deterministic across independent plugin instances.
    {
        const std::wstring key = L"9000abcd-1234-5678-abcd-9000abcdef12";
        WindowsConsoleLinkSession first;
        const auto firstResult = first.attach(key, 48000);
        assert(firstResult == WindowsConsoleLinkSession::AttachResult::created
               || firstResult == WindowsConsoleLinkSession::AttachResult::attached);
        assert(first.get() != nullptr);
        WindowsConsoleLinkSession second;
        assert(second.attach(key, 48000) == WindowsConsoleLinkSession::AttachResult::attached);
        assert(second.get() != nullptr);
        assert(first.mappingName() == second.mappingName());
        assert(first.sessionNonce() == second.sessionNonce());
        WindowsConsoleLinkSession wrongRate;
        assert(wrongRate.attach(key, 44100) == WindowsConsoleLinkSession::AttachResult::incompatible);
        WindowsConsoleLinkSession invalid;
        assert(invalid.attach(L"not-a-valid-session-key", 48000)
               == WindowsConsoleLinkSession::AttachResult::invalidKey);
    }
    // Concurrent attachers must serialize creation: exactly one initializes,
    // while the other validates and attaches to the completed mapping.
    {
        const auto key = std::wstring(L"9000c0de-")
            + std::to_wstring(GetCurrentProcessId())
            + std::to_wstring(GetTickCount64());
        WindowsConsoleLinkSession first;
        WindowsConsoleLinkSession second;
        WindowsConsoleLinkSession::AttachResult firstResult =
            WindowsConsoleLinkSession::AttachResult::failed;
        WindowsConsoleLinkSession::AttachResult secondResult =
            WindowsConsoleLinkSession::AttachResult::failed;

        std::thread firstThread([&] { firstResult = first.attach(key, 48000); });
        std::thread secondThread([&] { secondResult = second.attach(key, 48000); });
        firstThread.join();
        secondThread.join();

        const bool firstUsable = firstResult == WindowsConsoleLinkSession::AttachResult::created
            || firstResult == WindowsConsoleLinkSession::AttachResult::attached;
        const bool secondUsable = secondResult == WindowsConsoleLinkSession::AttachResult::created
            || secondResult == WindowsConsoleLinkSession::AttachResult::attached;
        if (!firstUsable || !secondUsable) {
            std::fprintf(stderr, "Concurrent attach failed: first=%d second=%d (created=1, attached=0, invalidKey=2, incompatible=3, failed=4)\\n",
                         static_cast<int>(firstResult), static_cast<int>(secondResult));
        }
        assert(firstUsable && secondUsable);
        assert((firstResult == WindowsConsoleLinkSession::AttachResult::created)
               != (secondResult == WindowsConsoleLinkSession::AttachResult::created));
        assert(first.get() != nullptr && second.get() != nullptr);
        assert(first.mappingName() == second.mappingName());
    }
    // End-to-end test: a separate process opens the mapping and consumes audio.
    {
        const auto uniqueName = std::wstring(L"Local\\SSL9000CrossProcess_")
            + std::to_wstring(GetCurrentProcessId()) + L"_"
            + std::to_wstring(GetTickCount64());
        WindowsSharedMemoryRegion creator;
        assert(creator.create(uniqueName, sizeof(SharedSessionMemory))
               == WindowsSharedMemoryRegion::CreateResult::created);
        auto* session = initializeNewSharedSessionMapping(creator, 48000, 0x90005678u);
        assert(session != nullptr);

        std::array<wchar_t, 32768> executable {};
        const auto pathLength = GetModuleFileNameW(nullptr, executable.data(),
                                                   static_cast<DWORD>(executable.size()));
        assert(pathLength > 0 && pathLength < executable.size());
        const std::wstring executablePath(executable.data(), pathLength);
        std::wstring command = L"\"" + executablePath
            + L"\" --consumer \"" + uniqueName + L"\"";
        std::vector<wchar_t> mutableCommand(command.begin(), command.end());
        mutableCommand.push_back(L'\0');

        STARTUPINFOW startup {};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process {};
        const BOOL childStarted = CreateProcessW(
            executablePath.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
        if (!childStarted) {
            const DWORD error = GetLastError();
            std::fwprintf(stderr, L"CreateProcessW failed: error=%lu, executable=%ls\n",
                     error, executablePath.c_str());
            return 14;
        }

        SharedSpscAudioRing sender(session->channelToBus[5]);
        const float audio[] { 0.125f, -0.125f, 0.25f, -0.25f };
        BlockHeader block {};
        block.projectSamplePosition = 1234567;
        block.sampleCount = 2;
        block.channels = stereoChannels;
        const auto pushDeadline = GetTickCount64() + 5000;
        bool pushed = false;
        while (GetTickCount64() < pushDeadline && !pushed) {
            pushed = sender.push(block, audio);
            if (!pushed)
                Sleep(1);
        }
        assert(pushed);

        const auto waitResult = WaitForSingleObject(process.hProcess, 10000);
        if (waitResult != WAIT_OBJECT_0)
            TerminateProcess(process.hProcess, 99);
        DWORD childExitCode = 0;
        const bool gotExitCode = GetExitCodeProcess(process.hProcess, &childExitCode) != FALSE;
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        assert(waitResult == WAIT_OBJECT_0);
        assert(gotExitCode && childExitCode == 0);
    }
#endif

    // Bus receiver aligns packets by absolute sample position, sums sources,
    // retains future/partial packets, and rejects stale or misrouted blocks.
    {
        auto session = std::make_unique<SharedSessionMemory>();
        assert(initializeSharedSession(*session, 48000, 0x9000b055u));
        auto receiver = std::make_unique<BusAudioReceiver>(*session);

        SharedSpscAudioRing source0(session->channelToBus[0]);
        SharedSpscAudioRing source1(session->channelToBus[1]);
        SharedSpscAudioRing source2(session->channelToBus[2]);
        SharedSpscAudioRing source3(session->channelToBus[3]);
        SharedSpscAudioRing source4(session->channelToBus[4]);

        const float earlier[] { 1.0f, 10.0f, 2.0f, 20.0f, 3.0f, 30.0f, 4.0f, 40.0f };
        const float aligned[] { 0.5f, -0.5f, 1.5f, -1.5f, 2.5f, -2.5f, 3.5f, -3.5f };
        BlockHeader h {};
        h.sampleCount = 4;
        h.projectSamplePosition = 98;
        h.sourceId = 0;
        h.busId = 2;
        assert(source0.push(h, earlier));
        h.projectSamplePosition = 100;
        h.sourceId = 1;
        assert(source1.push(h, aligned));

        // A future packet is held; a wrong-bus packet and stale packet are dropped.
        const float future[] { 0.25f, -0.25f, 0.5f, -0.5f };
        h.projectSamplePosition = 110;
        h.sampleCount = 2;
        h.sourceId = 2;
        assert(source2.push(h, future));
        h.projectSamplePosition = 100;
        h.sourceId = 3;
        h.busId = 1;
        assert(source3.push(h, future));
        h.projectSamplePosition = 90;
        h.sourceId = 4;
        h.busId = 2;
        h.sampleCount = 1;
        assert(source4.push(h, future));

        std::array<float, 4> outL {}, outR {};
        assert(receiver->process(2, 100, 4, outL.data(), outR.data()));
        assert(std::abs(outL[0] - 3.5f) < 1.0e-6f);
        assert(std::abs(outR[0] - 29.5f) < 1.0e-6f);
        assert(std::abs(outL[1] - 5.5f) < 1.0e-6f);
        assert(std::abs(outR[1] - 38.5f) < 1.0e-6f);
        assert(std::abs(outL[2] - 2.5f) < 1.0e-6f);
        assert(std::abs(outR[2] + 2.5f) < 1.0e-6f);
        assert(std::abs(outL[3] - 3.5f) < 1.0e-6f);
        assert(std::abs(outR[3] + 3.5f) < 1.0e-6f);

        // Source 2's future block is retained until its timestamp is requested.
        assert(receiver->process(2, 108, 2, outL.data(), outR.data()));
        assert(outL[0] == 0.0f && outL[1] == 0.0f);
        assert(receiver->process(2, 110, 2, outL.data(), outR.data()));
        assert(std::abs(outL[0] - 0.25f) < 1.0e-6f);
        assert(std::abs(outR[1] + 0.5f) < 1.0e-6f);

        // A single larger packet can be consumed across smaller host callbacks.
        const float split[] { 0.1f, -0.1f, 0.2f, -0.2f, 0.3f, -0.3f, 0.4f, -0.4f };
        h.projectSamplePosition = 200;
        h.sampleCount = 4;
        h.sourceId = 5;
        h.busId = 2;
        SharedSpscAudioRing source5(session->channelToBus[5]);
        assert(source5.push(h, split));
        assert(receiver->process(2, 200, 2, outL.data(), outR.data()));
        assert(std::abs(outL[0] - 0.1f) < 1.0e-6f);
        assert(std::abs(outL[1] - 0.2f) < 1.0e-6f);
        assert(receiver->process(2, 202, 2, outL.data(), outR.data()));
        assert(std::abs(outL[0] - 0.3f) < 1.0e-6f);
        assert(std::abs(outL[1] - 0.4f) < 1.0e-6f);

        const auto& stats = receiver->getDiagnostics();
        assert(stats.wrongRouteBlocks >= 1);
        assert(stats.staleBlocks >= 1);
        assert(!receiver->process(32, 0, 1, outL.data(), outR.data()));
        assert(!receiver->process(2, -1, 1, outL.data(), outR.data()));
    }

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
        SharedSpscAudioRing secondChannelRing(session->channelToBus[8]);
        SharedSpscAudioRing masterRing(session->busToMaster[3]);
        std::array<float, 2> frame { 0.25f, -0.25f };
        std::array<float, 2> receivedFrame {};
        // Channel publisher binds source and destination identity to each block.
        {
            ChannelAudioPublisher publisher(session->channelToBus[9], 9, 6);
            assert(publisher.isConfigured());
            assert(publisher.publish(1200, 1, frame.data()));
            BlockHeader publishedHeader {};
            SharedSpscAudioRing busSixReceiver(session->channelToBus[9]);
            assert(busSixReceiver.pop(publishedHeader, receivedFrame.data(), 1));
            assert(publishedHeader.sourceId == 9 && publishedHeader.busId == 6);
            assert(publishedHeader.projectSamplePosition == 1200);
            assert(receivedFrame == frame);
            ChannelAudioPublisher invalidPublisher(session->channelToBus[10], 32, 6);
            assert(!invalidPublisher.isConfigured());
            assert(!invalidPublisher.publish(1201, 1, frame.data()));
        }

        BlockHeader h {};
        h.sampleCount = 1;
        h.projectSamplePosition = 900;
        h.busId = 3;
        h.sourceId = 7;
        assert(channelRing.push(h, frame.data()));
        BlockHeader receivedHeader {};
        assert(channelRing.pop(receivedHeader, receivedFrame.data(), 1));
        assert(receivedHeader.projectSamplePosition == 900);
        assert(receivedHeader.busId == 3 && receivedHeader.sourceId == 7);
        assert(receivedFrame == frame);

        // A second channel can target a different bus without sharing its ring.
        h.busId = 4;
        h.sourceId = 8;
        h.projectSamplePosition = 901;
        assert(secondChannelRing.push(h, frame.data()));
        assert(secondChannelRing.pop(receivedHeader, receivedFrame.data(), 1));
        assert(receivedHeader.busId == 4 && receivedHeader.sourceId == 8);
        assert(receivedHeader.projectSamplePosition == 901);

        h.busId = 3;
        h.sourceId = 7;
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
