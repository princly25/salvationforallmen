#pragma once

#include "core/ProfileConfig.hpp"

#include <QString>
#include <QWebEngineScript>

class ProfileSeedEngine;
class QWebEngineProfile;

class FingerprintEngine {
public:
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

