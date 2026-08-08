#pragma once

#include "core/ProfileConfig.hpp"

#include <QString>
#include <QWebEngineScript>

#include <cstdint>

class ProfileSeedEngine;
class QWebEngineProfile;

struct FingerprintNoiseParameters {
    std::uint32_t canvasSeed{0};
    std::uint32_t webglSeed{0};
    std::uint32_t audioSeed{0};
    int canvasBitShift{1};
    int webglParameterOffset{1};
    double audioFrequencyOffset{0.0};

    bool operator==(const FingerprintNoiseParameters&) const = default;
};

class FingerprintEngine {
public:
    [[nodiscard]] static FingerprintNoiseParameters
    deriveNoiseParameters(const ProfileSeedEngine& seedEngine);
    [[nodiscard]] static QString generateInjectionScript(const ProfileConfig& config,
                                                         const ProfileSeedEngine& seedEngine);
    [[nodiscard]] static QWebEngineScript buildScript(const ProfileConfig& config,
                                                      const ProfileSeedEngine& seedEngine);
    static void install(QWebEngineProfile& profile, const ProfileConfig& config,
                        const ProfileSeedEngine& seedEngine);

private:
    [[nodiscard]] static QString javascriptLiteral(const QString& value);
    [[nodiscard]] static QString javascriptStringArray(const QStringList& values);
};
