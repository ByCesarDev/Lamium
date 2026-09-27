#include "features/information/SystemInfo.h"
#include <Windows.h>
#include <psapi.h>
#include <winternl.h>
#include <algorithm>
#include <chrono>
#include <format>

namespace lamium::information {
namespace {
std::optional<std::string> registryString(HKEY root, wchar_t const* path, wchar_t const* name) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, path, 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) return {};
    DWORD type = 0, size = 0;
    std::optional<std::string> result;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) == ERROR_SUCCESS
        && (type == REG_SZ || type == REG_EXPAND_SZ) && size > sizeof(wchar_t)) {
        std::wstring wide(size / sizeof(wchar_t), L'\0');
        if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(wide.data()), &size) == ERROR_SUCCESS) {
            while (!wide.empty() && wide.back() == L'\0') wide.pop_back();
            int needed = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
            if (needed > 0) {
                std::string text(static_cast<size_t>(needed), '\0');
                WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), text.data(), needed, nullptr, nullptr);
                result = std::move(text);
            }
        }
    }
    RegCloseKey(key);
    return result;
}
}
std::optional<std::string> systemMemoryText() {
    static std::chrono::steady_clock::time_point sampled{};
    static std::optional<std::string> cached;
    auto now = std::chrono::steady_clock::now();
    if (cached && now - sampled < std::chrono::milliseconds(500)) return cached;
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (!K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        cached.reset();
        sampled = now;
        return {};
    }
    constexpr double gib = 1024.0 * 1024.0 * 1024.0;
    cached = std::format("{:.1f} GB (peak {:.1f} GB)", counters.WorkingSetSize / gib, counters.PeakWorkingSetSize / gib);
    sampled = now;
    return cached;
}
std::optional<std::string> systemCpuText() {
    static std::optional<std::string> cached = []() -> std::optional<std::string> {
        SYSTEM_INFO info{};
        GetNativeSystemInfo(&info);
        auto name = registryString(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                                   L"ProcessorNameString");
        unsigned threads = info.dwNumberOfProcessors;
        if (name && threads) return std::format("{} ({} threads)", *name, threads);
        if (name) return name;
        if (threads) return std::format("{} threads", threads);
        return {};
    }();
    return cached;
}
std::optional<std::string> systemGpuText() {
    static std::optional<std::string> cached = []() -> std::optional<std::string> {
        DISPLAY_DEVICEW device{};
        device.cb = sizeof(device);
        if (!EnumDisplayDevicesW(nullptr, 0, &device, 0)) return {};
        if (device.DeviceString[0] == L'\0') return {};
        int needed = WideCharToMultiByte(CP_UTF8, 0, device.DeviceString, -1, nullptr, 0, nullptr, nullptr);
        if (needed <= 1) return {};
        std::string text(static_cast<size_t>(needed), '\0');
        WideCharToMultiByte(CP_UTF8, 0, device.DeviceString, -1, text.data(), needed, nullptr, nullptr);
        text.pop_back();
        return text;
    }();
    return cached;
}
std::optional<std::string> systemDisplayText() {
    static std::optional<std::string> cached = []() -> std::optional<std::string> {
        int width = GetSystemMetrics(SM_CXSCREEN), height = GetSystemMetrics(SM_CYSCREEN);
        if (width <= 0 || height <= 0) return {};
        return std::format("{} × {}", width, height);
    }();
    return cached;
}
std::optional<std::string> systemOsText() {
    static std::optional<std::string> cached = []() -> std::optional<std::string> {
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (!ntdll) return {};
        using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
        auto getVersion = reinterpret_cast<RtlGetVersionFn>(
            reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")));
        if (!getVersion) return {};
        RTL_OSVERSIONINFOW version{};
        version.dwOSVersionInfoSize = sizeof(version);
        if (getVersion(&version) != 0) return {};
        char const* name = version.dwBuildNumber >= 22000 ? "Windows 11"
            : version.dwMajorVersion == 10 ? "Windows 10" : nullptr;
        if (name) return std::format("{} ({})", name, version.dwBuildNumber);
        return std::format("Windows {}.{} ({})", version.dwMajorVersion, version.dwMinorVersion,
                           version.dwBuildNumber);
    }();
    return cached;
}
}
