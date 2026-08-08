#include "core/ProfileValidator.hpp"

#include "core/ProfileSandbox.hpp"

#include <QRegularExpression>

ValidationResult ProfileValidator::validateProfile(const ProfileConfig& config)
{
    ValidationResult result;
    const auto reject = [&result](const QString& message) {
        result.isValid = false;
        result.discrepancies.append(message);
    };

    if (!ProfileSandbox::isSafeProfileId(config.id)) {
        reject(QStringLiteral("ERROR: Profile id must be path-safe and 1-64 characters."));
    }
    if (config.name.trimmed().isEmpty()) {
        reject(QStringLiteral("ERROR: Profile name cannot be empty."));
    }
    if (config.userAgent.trimmed().isEmpty()) {
        reject(QStringLiteral("ERROR: User-Agent cannot be empty."));
    }
    if (config.userAgent.contains(QStringLiteral("Windows"), Qt::CaseInsensitive)
        && QString::fromStdString(config.hardware.webglRenderer)
               .contains(QStringLiteral("Apple"), Qt::CaseInsensitive)) {
        reject(QStringLiteral("CRITICAL: Windows User-Agent paired with Apple WebGL renderer."));
    }

    static const QRegularExpression seedPattern(QStringLiteral("^[0-9A-Fa-f]{64}$"));
    if (!seedPattern.match(QString::fromStdString(config.hardware.masterSeedHex)).hasMatch()) {
        reject(QStringLiteral("ERROR: Master seed must contain exactly 64 hexadecimal characters."));
    }
    if (config.hardware.cpuCores < 1 || config.hardware.cpuCores > 256) {
        reject(QStringLiteral("ERROR: CPU core count is outside the supported range."));
    }
    if (config.hardware.memoryGb < 1 || config.hardware.memoryGb > 1024) {
        reject(QStringLiteral("ERROR: Memory size is outside the supported range."));
    }
    if (config.hardware.screenWidth <= 0 || config.hardware.screenHeight <= 0) {
        reject(QStringLiteral("ERROR: Invalid screen dimensions."));
    }
    if (config.languages.isEmpty()) {
        reject(QStringLiteral("ERROR: At least one navigator language is required."));
    }
    if (config.timezone.trimmed().isEmpty()) {
        reject(QStringLiteral("ERROR: Timezone cannot be empty."));
    }
    if (config.proxy.type() != QNetworkProxy::NoProxy
        && (config.proxy.hostName().trimmed().isEmpty() || config.proxy.port() == 0)) {
        reject(QStringLiteral("ERROR: Proxy host and port are required when proxying is enabled."));
    }

    return result;
}

