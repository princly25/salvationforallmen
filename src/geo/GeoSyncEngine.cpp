#include "geo/GeoSyncEngine.hpp"

#include <QDateTime>
#include <QHash>
#include <QLocale>
#include <QTimeZone>

#include <cstring>

GeoSyncEngine::GeoSyncEngine(const std::string& mmdbPath)
{
    const int status = MMDB_open(mmdbPath.c_str(), MMDB_MODE_MMAP, &m_mmdb);
    if (status == MMDB_SUCCESS) {
        m_isOpen = true;
    } else {
        m_lastError = QString::fromUtf8(MMDB_strerror(status));
    }
}

GeoSyncEngine::~GeoSyncEngine()
{
    if (m_isOpen) {
        MMDB_close(&m_mmdb);
    }
}

bool GeoSyncEngine::isOpen() const noexcept
{
    return m_isOpen;
}

QString GeoSyncEngine::lastError() const
{
    return m_lastError;
}

QString GeoSyncEngine::readUtf8(MMDB_entry_s entry, const char* first, const char* second)
{
    MMDB_entry_data_s data{};
    const int status = MMDB_get_value(&entry, &data, first, second, nullptr);
    if (status != MMDB_SUCCESS || !data.has_data || data.type != MMDB_DATA_TYPE_UTF8_STRING) {
        return {};
    }
    return QString::fromUtf8(data.utf8_string, static_cast<qsizetype>(data.data_size));
}

std::optional<double> GeoSyncEngine::readDouble(MMDB_entry_s entry, const char* first,
                                                const char* second)
{
    MMDB_entry_data_s data{};
    const int status = MMDB_get_value(&entry, &data, first, second, nullptr);
    if (status != MMDB_SUCCESS || !data.has_data) {
        return std::nullopt;
    }
    if (data.type == MMDB_DATA_TYPE_DOUBLE) {
        return data.double_value;
    }
    if (data.type == MMDB_DATA_TYPE_FLOAT) {
        return static_cast<double>(data.float_value);
    }
    return std::nullopt;
}

QStringList GeoSyncEngine::languagesForCountry(const QString& countryCode)
{
    static const QHash<QString, QStringList> languages{
        {QStringLiteral("US"), {QStringLiteral("en-US"), QStringLiteral("en")}},
        {QStringLiteral("GB"), {QStringLiteral("en-GB"), QStringLiteral("en")}},
        {QStringLiteral("CA"), {QStringLiteral("en-CA"), QStringLiteral("fr-CA"), QStringLiteral("en")}},
        {QStringLiteral("DE"), {QStringLiteral("de-DE"), QStringLiteral("de"), QStringLiteral("en")}},
        {QStringLiteral("FR"), {QStringLiteral("fr-FR"), QStringLiteral("fr"), QStringLiteral("en")}},
        {QStringLiteral("ES"), {QStringLiteral("es-ES"), QStringLiteral("es"), QStringLiteral("en")}},
        {QStringLiteral("BR"), {QStringLiteral("pt-BR"), QStringLiteral("pt"), QStringLiteral("en")}},
        {QStringLiteral("JP"), {QStringLiteral("ja-JP"), QStringLiteral("ja"), QStringLiteral("en")}},
        {QStringLiteral("CN"), {QStringLiteral("zh-CN"), QStringLiteral("zh"), QStringLiteral("en")}},
        {QStringLiteral("IN"), {QStringLiteral("en-IN"), QStringLiteral("hi-IN"), QStringLiteral("en")}},
    };
    const QString normalizedCountry = countryCode.toUpper();
    if (const auto known = languages.constFind(normalizedCountry); known != languages.cend()) {
        return known.value();
    }

    const QLocale::Territory territory = QLocale::codeToTerritory(normalizedCountry);
    if (territory != QLocale::AnyTerritory) {
        const QList<QLocale> locales =
            QLocale::matchingLocales(QLocale::AnyLanguage, QLocale::AnyScript, territory);
        for (const QLocale& locale : locales) {
            if (locale.language() == QLocale::C) {
                continue;
            }
            const QString regionalLanguage = locale.bcp47Name();
            const QString baseLanguage = QLocale::languageToCode(locale.language());
            if (!regionalLanguage.isEmpty() && !baseLanguage.isEmpty()) {
                return {regionalLanguage, baseLanguage};
            }
        }
    }
    return {QStringLiteral("en-US"), QStringLiteral("en")};
}

std::optional<GeoLocationData> GeoSyncEngine::resolveProxyIp(const QString& ipAddress)
{
    m_lastError.clear();
    if (!m_isOpen) {
        m_lastError = QStringLiteral("GeoIP database is not open");
        return std::nullopt;
    }
    if (ipAddress.trimmed().isEmpty() || ipAddress.contains(QChar::Null)) {
        m_lastError = QStringLiteral("Proxy IP address is invalid");
        return std::nullopt;
    }

    const QByteArray encodedAddress = ipAddress.trimmed().toLatin1();
    int addressError = 0;
    int databaseError = MMDB_SUCCESS;
    const MMDB_lookup_result_s lookup =
        MMDB_lookup_string(&m_mmdb, encodedAddress.constData(), &addressError, &databaseError);
    if (addressError != 0) {
        m_lastError = QStringLiteral("Proxy IP address could not be parsed");
        return std::nullopt;
    }
    if (databaseError != MMDB_SUCCESS) {
        m_lastError = QString::fromUtf8(MMDB_strerror(databaseError));
        return std::nullopt;
    }
    if (!lookup.found_entry) {
        m_lastError = QStringLiteral("Proxy IP address was not found in the GeoIP database");
        return std::nullopt;
    }

    GeoLocationData location;
    location.countryCode = readUtf8(lookup.entry, "country", "iso_code").toUpper();
    if (location.countryCode.isEmpty()) {
        location.countryCode =
            readUtf8(lookup.entry, "registered_country", "iso_code").toUpper();
    }
    location.timezone = readUtf8(lookup.entry, "location", "time_zone");
    location.latitude = readDouble(lookup.entry, "location", "latitude").value_or(0.0);
    location.longitude = readDouble(lookup.entry, "location", "longitude").value_or(0.0);
    location.languages = languagesForCountry(location.countryCode);

    const QTimeZone timezone(location.timezone.toUtf8());
    if (timezone.isValid()) {
        location.timezoneOffsetMinutes =
            timezone.offsetFromUtc(QDateTime::currentDateTimeUtc()) / 60;
    }
    return location;
}
