#pragma once

#include <QString>
#include <QStringList>

#include <maxminddb.h>

#include <optional>
#include <string>

struct GeoLocationData {
    QString countryCode;
    QString timezone;
    QStringList languages;
    double latitude{0.0};
    double longitude{0.0};
    int timezoneOffsetMinutes{0};
};

class GeoSyncEngine {
public:
    explicit GeoSyncEngine(const std::string& mmdbPath);
    ~GeoSyncEngine();

    GeoSyncEngine(const GeoSyncEngine&) = delete;
    GeoSyncEngine& operator=(const GeoSyncEngine&) = delete;

    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] QString lastError() const;
    [[nodiscard]] std::optional<GeoLocationData> resolveProxyIp(const QString& ipAddress);

private:
    [[nodiscard]] static QString readUtf8(MMDB_entry_s entry, const char* first,
                                          const char* second);
    [[nodiscard]] static std::optional<double> readDouble(MMDB_entry_s entry,
                                                          const char* first, const char* second);
    [[nodiscard]] static QStringList languagesForCountry(const QString& countryCode);

    MMDB_s m_mmdb{};
    bool m_isOpen{false};
    QString m_lastError;
};
