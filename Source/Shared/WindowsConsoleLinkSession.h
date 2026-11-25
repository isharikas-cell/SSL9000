#pragma once

#include "WindowsSharedMemory.h"

#if defined(_WIN32)
#include <cstdint>
#include <string>

namespace ssl9000::link {

// Explicit project/session token is supplied by the user or host integration.
// Do not derive this from a process ID: separate plugin processes must agree.
class WindowsConsoleLinkSession {
public:
    enum class AttachResult { attached, created, invalidKey, incompatible, abandonedInitialization, failed };

    AttachResult attach(const std::wstring& projectSessionKey,
                        std::uint32_t sampleRate) {
        region.close();
        session = nullptr;
        if (!isValidKey(projectSessionKey) || sampleRate == 0)
            return AttachResult::invalidKey;

        const auto hashA = hash(projectSessionKey, 14695981039346656037ull);
        const auto hashB = hash(projectSessionKey, 1099511628211ull ^ 0x9e3779b97f4a7c15ull);
        name = L"Local\\SSL9000_" + toHex(hashA) + toHex(hashB);
        nonce = hashA ^ (hashB + 0x9e3779b97f4a7c15ull);
        if (nonce == 0)
            nonce = 1;

        // Serialize creator initialization and opener validation before either
        // process touches the C++ objects in the mapped view.
        InitializationMutexGuard initializationLock(name + L"_Init");
        if (!initializationLock.acquired())
            return initializationLock.wasAbandoned()
                ? AttachResult::abandonedInitialization : AttachResult::failed;

        const auto result = region.create(name, sizeof(SharedSessionMemory));
        if (result == WindowsSharedMemoryRegion::CreateResult::failed)
            return AttachResult::failed;

        if (result == WindowsSharedMemoryRegion::CreateResult::created) {
            session = initializeNewSharedSessionMapping(region, sampleRate, nonce);
            return session != nullptr ? AttachResult::created : AttachResult::failed;
        }

        session = validateSharedSessionMapping(region);
        if (session == nullptr || session->header.sessionNonce != nonce
            || session->header.sampleRate != sampleRate) {
            session = nullptr;
            return AttachResult::incompatible;
        }
        return AttachResult::attached;
    }

    SharedSessionMemory* get() noexcept { return session; }
    const std::wstring& mappingName() const noexcept { return name; }
    std::uint64_t sessionNonce() const noexcept { return nonce; }
    void close() noexcept { session = nullptr; region.close(); }

private:
    static constexpr DWORD initializationMutexWaitMilliseconds = 5000;

    class InitializationMutexGuard {
    public:
        explicit InitializationMutexGuard(const std::wstring& mutexName) noexcept {
            handle = CreateMutexW(nullptr, FALSE, mutexName.c_str());
            if (handle == nullptr)
                return;

            const auto waitResult = WaitForSingleObject(handle, initializationMutexWaitMilliseconds);
            // WAIT_ABANDONED means a previous creator may have died while the
            // mapping's C++ object lifetime was only partially initialized.
            // Do not inspect or overwrite that mapping automatically.
            ownsMutex = waitResult == WAIT_OBJECT_0 || waitResult == WAIT_ABANDONED;
            safeToProceed = waitResult == WAIT_OBJECT_0;
            abandoned = waitResult == WAIT_ABANDONED;
        }

        InitializationMutexGuard(const InitializationMutexGuard&) = delete;
        InitializationMutexGuard& operator=(const InitializationMutexGuard&) = delete;

        ~InitializationMutexGuard() {
            if (ownsMutex)
                ReleaseMutex(handle);
            if (handle != nullptr)
                CloseHandle(handle);
        }

        bool acquired() const noexcept { return safeToProceed; }
        bool wasAbandoned() const noexcept { return abandoned; }

    private:
        HANDLE handle = nullptr;
        bool ownsMutex = false;
        bool safeToProceed = false;
        bool abandoned = false;
    };

    static bool isValidKey(const std::wstring& key) noexcept {
        // Require a UUID-like explicit token; reject whitespace and path syntax.
        if (key.size() < 16 || key.size() > 64)
            return false;
        for (const auto ch : key) {
            const bool valid = (ch >= L'0' && ch <= L'9')
                || (ch >= L'a' && ch <= L'f') || (ch >= L'A' && ch <= L'F')
                || ch == L'-';
            if (!valid)
                return false;
        }
        return true;
    }

    static std::uint64_t hash(const std::wstring& key, std::uint64_t seed) noexcept {
        auto value = seed;
        for (const auto ch : key) {
            value ^= static_cast<std::uint64_t>(ch);
            value *= 1099511628211ull;
        }
        return value;
    }

    static std::wstring toHex(std::uint64_t value) {
        constexpr wchar_t digits[] = L"0123456789abcdef";
        std::wstring result(16, L'0');
        for (int i = 15; i >= 0; --i) {
            result[static_cast<std::size_t>(i)] = digits[value & 0xfu];
            value >>= 4;
        }
        return result;
    }

    WindowsSharedMemoryRegion region;
    SharedSessionMemory* session = nullptr;
    std::wstring name;
    std::uint64_t nonce = 0;
};

} // namespace ssl9000::link
#endif // _WIN32
