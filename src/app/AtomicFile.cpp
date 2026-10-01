#include "app/AtomicFile.h"
#include <atomic>
#include <string>
#include <system_error>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace lamium {
void writeFileReplacing(std::filesystem::path const& path, std::string_view text, char const* what) {
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    static std::atomic<uint64_t> sequence{0};
    std::filesystem::path temporary;
    HANDLE handle = INVALID_HANDLE_VALUE;
    auto fail = [&](char const* action) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                std::string(action) + " " + what);
    };
    for (int attempt = 0; attempt < 128; ++attempt) {
        temporary = path;
        temporary += "." + std::to_string(GetCurrentProcessId()) + "." + std::to_string(sequence++) + ".tmp";
        handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle != INVALID_HANDLE_VALUE) break;
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) fail("Creating a temporary file for");
    }
    if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error(std::string("Could not reserve a temporary file for ") + what);
    try {
        DWORD written = 0;
        if (!WriteFile(handle, text.data(), static_cast<DWORD>(text.size()), &written, nullptr)) fail("Writing");
        if (written != text.size()) throw std::runtime_error(std::string("Incomplete write of ") + what);
        if (!FlushFileBuffers(handle)) fail("Flushing");
        auto closing = handle;
        handle = INVALID_HANDLE_VALUE;
        if (!CloseHandle(closing)) fail("Closing the temporary file for");
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            fail("Replacing");
    } catch (...) {
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}
}
