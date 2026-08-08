#include "hooks/MinHookManager.hpp"

#ifdef _WIN32
#include <MinHook.h>
#include <windows.h>

#include <atomic>

namespace {
using GetSystemInfoFunction = void(WINAPI*)(LPSYSTEM_INFO);
using GlobalMemoryStatusExFunction = BOOL(WINAPI*)(LPMEMORYSTATUSEX);

GetSystemInfoFunction originalGetSystemInfo = nullptr;
GetSystemInfoFunction originalGetNativeSystemInfo = nullptr;
GlobalMemoryStatusExFunction originalGlobalMemoryStatusEx = nullptr;
std::atomic<DWORD> targetCpuCores{8};
std::atomic<DWORDLONG> targetMemoryBytes{16ULL * 1024ULL * 1024ULL * 1024ULL};

void WINAPI hookedGetSystemInfo(LPSYSTEM_INFO information)
{
    originalGetSystemInfo(information);
    information->dwNumberOfProcessors = targetCpuCores.load(std::memory_order_relaxed);
}

void WINAPI hookedGetNativeSystemInfo(LPSYSTEM_INFO information)
{
    originalGetNativeSystemInfo(information);
    information->dwNumberOfProcessors = targetCpuCores.load(std::memory_order_relaxed);
}

BOOL WINAPI hookedGlobalMemoryStatusEx(LPMEMORYSTATUSEX status)
{
    const BOOL result = originalGlobalMemoryStatusEx(status);
    if (result) {
        status->ullTotalPhys = targetMemoryBytes.load(std::memory_order_relaxed);
    }
    return result;
}

bool createAndEnableHook(LPVOID target, LPVOID replacement, LPVOID* original)
{
    return MH_CreateHook(target, replacement, original) == MH_OK && MH_EnableHook(target) == MH_OK;
}
}
#endif

MinHookManager::~MinHookManager()
{
    shutdown();
}

bool MinHookManager::initialize(const HardwareFingerprint& fingerprint)
{
    shutdown();
#ifdef _WIN32
    targetCpuCores.store(static_cast<DWORD>(fingerprint.cpuCores), std::memory_order_relaxed);
    targetMemoryBytes.store(static_cast<DWORDLONG>(fingerprint.memoryGb) * 1024ULL * 1024ULL * 1024ULL,
                            std::memory_order_relaxed);

    if (MH_Initialize() != MH_OK) {
        m_lastError = QStringLiteral("MinHook initialization failed");
        return false;
    }
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    const bool installed = kernel
        && createAndEnableHook(reinterpret_cast<LPVOID>(GetProcAddress(kernel, "GetSystemInfo")),
                               reinterpret_cast<LPVOID>(&hookedGetSystemInfo),
                               reinterpret_cast<LPVOID*>(&originalGetSystemInfo))
        && createAndEnableHook(reinterpret_cast<LPVOID>(GetProcAddress(kernel, "GetNativeSystemInfo")),
                               reinterpret_cast<LPVOID>(&hookedGetNativeSystemInfo),
                               reinterpret_cast<LPVOID*>(&originalGetNativeSystemInfo))
        && createAndEnableHook(reinterpret_cast<LPVOID>(GetProcAddress(kernel, "GlobalMemoryStatusEx")),
                               reinterpret_cast<LPVOID>(&hookedGlobalMemoryStatusEx),
                               reinterpret_cast<LPVOID*>(&originalGlobalMemoryStatusEx));
    if (!installed) {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        m_lastError = QStringLiteral("One or more Win32 hooks could not be installed");
        return false;
    }
    m_active = true;
    m_lastError.clear();
    return true;
#else
    (void)fingerprint;
    m_lastError = QStringLiteral("MinHook is available only in Windows builds");
    return false;
#endif
}

void MinHookManager::shutdown() noexcept
{
#ifdef _WIN32
    if (m_active) {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
    }
#endif
    m_active = false;
}

bool MinHookManager::isActive() const noexcept
{
    return m_active;
}

QString MinHookManager::lastError() const
{
    return m_lastError;
}

