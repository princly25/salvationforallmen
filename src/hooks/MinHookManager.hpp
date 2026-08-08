#pragma once

#include "core/ProfileConfig.hpp"

#include <QString>

class MinHookManager {
public:
    MinHookManager() = default;
    ~MinHookManager();

    MinHookManager(const MinHookManager&) = delete;
    MinHookManager& operator=(const MinHookManager&) = delete;

    [[nodiscard]] static constexpr bool platformSupported() noexcept
    {
#ifdef _WIN32
        return true;
#else
        return false;
#endif
    }

    bool initialize(const HardwareFingerprint& fingerprint);
    void shutdown() noexcept;
    [[nodiscard]] bool isActive() const noexcept;
    [[nodiscard]] QString lastError() const;

private:
    bool m_active{false};
    QString m_lastError;
};

