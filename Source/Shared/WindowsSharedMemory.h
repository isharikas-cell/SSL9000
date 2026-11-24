#pragma once

#include "ConsoleLinkProtocol.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <string>

namespace ssl9000::link {

// Owns a named, session-local Windows file-mapping object and its mapped view.
// The caller supplies a unique name (normally under the Local\\ namespace)
// derived from a per-project/session identifier. Do not use a global fixed name.
class WindowsSharedMemoryRegion {
public:
    enum class CreateResult { created, alreadyExists, failed };

    WindowsSharedMemoryRegion() = default;
    WindowsSharedMemoryRegion(const WindowsSharedMemoryRegion&) = delete;
    WindowsSharedMemoryRegion& operator=(const WindowsSharedMemoryRegion&) = delete;

    WindowsSharedMemoryRegion(WindowsSharedMemoryRegion&& other) noexcept {
        swap(other);
    }

    WindowsSharedMemoryRegion& operator=(WindowsSharedMemoryRegion&& other) noexcept {
        if (this != &other) {
            close();
            swap(other);
        }
        return *this;
    }

    ~WindowsSharedMemoryRegion() { close(); }

    CreateResult create(const std::wstring& name, std::size_t bytes) noexcept {
        close();
        if (!isValidName(name) || bytes == 0)
            return CreateResult::failed;

        const auto size = static_cast<std::uint64_t>(bytes);
        HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
                                             PAGE_READWRITE,
                                             static_cast<DWORD>(size >> 32),
                                             static_cast<DWORD>(size & 0xffffffffu),
                                             name.c_str());
        if (mapping == nullptr)
            return CreateResult::failed;

        const bool existed = GetLastError() == ERROR_ALREADY_EXISTS;
        void* view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, bytes);
        if (view == nullptr) {
            CloseHandle(mapping);
            return CreateResult::failed;
        }

        handle = mapping;
        mappedView = view;
        mappedBytes = bytes;
        return existed ? CreateResult::alreadyExists : CreateResult::created;
    }

    bool open(const std::wstring& name, std::size_t bytes) noexcept {
        close();
        if (!isValidName(name) || bytes == 0)
            return false;

        HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
        if (mapping == nullptr)
            return false;

        void* view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, bytes);
        if (view == nullptr) {
            CloseHandle(mapping);
            return false;
        }

        handle = mapping;
        mappedView = view;
        mappedBytes = bytes;
        return true;
    }

    void close() noexcept {
        if (mappedView != nullptr) {
            UnmapViewOfFile(mappedView);
            mappedView = nullptr;
        }
        if (handle != nullptr) {
            CloseHandle(handle);
            handle = nullptr;
        }
        mappedBytes = 0;
    }

    void* data() noexcept { return mappedView; }
    const void* data() const noexcept { return mappedView; }
    std::size_t size() const noexcept { return mappedBytes; }
    explicit operator bool() const noexcept { return mappedView != nullptr; }

private:
    static bool isValidName(const std::wstring& name) noexcept {
        // Require an explicit session-local namespace; avoid accidental
        // machine-global mappings and ambiguous unqualified names.
        return name.rfind(L"Local\\", 0) == 0 && name.size() > 6;
    }

    void swap(WindowsSharedMemoryRegion& other) noexcept {
        std::swap(handle, other.handle);
        std::swap(mappedView, other.mappedView);
        std::swap(mappedBytes, other.mappedBytes);
    }

    HANDLE handle = nullptr;
    void* mappedView = nullptr;
    std::size_t mappedBytes = 0;
};

// Validate an opened mapping before treating it as a Console Link session.
// Only the process that received CreateResult::created may initialize it.
inline SharedSessionMemory* validateSharedSessionMapping(
    WindowsSharedMemoryRegion& region) noexcept {
    if (!region || region.size() < sizeof(SharedSessionMemory))
        return nullptr;
    auto* session = static_cast<SharedSessionMemory*>(region.data());
    return isCompatibleSession(*session) ? session : nullptr;
}

} // namespace ssl9000::link
#endif // _WIN32
