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
    enum class AttachResult { attached, created, invalidKey, incompatible, failed };

    AttachResult attach(const std::wstring& projectSessionKey,
                        std::uint32_t sampleRate) {
        region.close();
        if (!isValidKey(projectSessionKey) || sampleRate == 0)
            return AttachResult::invalidKey;

        const auto hashA = hash(projectSessionKey, 14695981039346656037ull);
        const auto hashB = hash(projectSessionKey, 1099511628211ull ^ 0x9e3779b97f4a7c15ull);
        name = L"Local\\SSL9000_" + toHex(hashA) + toHex(hashB);
        nonce = hashA ^ (hashB + 0x9e3779b97f4a7c15ull);
        if (nonce == 0)
            nonce = 1;

        const auto result = region.create(name, sizeof(SharedSessionMemory));
        if (result == WindowsSharedMemoryRegion::CreateResult::failed)
            return AttachResult::failed;

        if (result == WindowsSharedMemoryRegion::CreateResult::created) {
            session = initializeNewSharedSessionMapping(region, sampleRate, nonce);
            return session != nullptr ? AttachResult::created : AttachResult::failed;
        }

        // CreateFileMapping can report an existing mapping while its creator is
        // still initializing the shared C++ objects. This bounded wait is only
        // used during attach (never from an audio callback).
        for (std::uint32_t attempt = 0;
             attempt < initializationWaitAttempts
                 && static_cast<SharedSessionMemory*>(region.data())->header.active.load(
                        std::memory_order_acquire) != 1;
             ++attempt) {
            Sleep(initializationPollMilliseconds);
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
    static constexpr std::uint32_t initializationWaitAttempts = 100;
    static constexpr DWORD initializationPollMilliseconds = 1;

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
