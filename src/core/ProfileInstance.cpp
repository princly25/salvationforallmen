#include "core/ProfileInstance.hpp"

#include "core/CustomUrlInterceptor.hpp"
#include "core/ProfileValidator.hpp"
#include "crypto/ProfileSeedEngine.hpp"
#include "geo/GeoSyncEngine.hpp"
#include "hooks/FingerprintEngine.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineView>

#include <stdexcept>

ProfileInstance::ProfileInstance(const ProfileConfig& config, const QString& storageRoot, QObject* parent)
    : QObject(parent)
    , m_config(config)
    , m_storageRoot(storageRoot)
{
    synchronizeProxyGeoLocation();

    const ValidationResult validation = ProfileValidator::validateProfile(m_config);
    if (!validation.isValid) {
        throw std::invalid_argument(validation.discrepancies.join(QStringLiteral(" ")).toStdString());
    }

    setupStoragePaths();
    const QString platform = m_config.userAgent.contains(QStringLiteral("Android"), Qt::CaseInsensitive)
        ? QStringLiteral("Android")
        : (m_config.userAgent.contains(QStringLiteral("iPhone"), Qt::CaseInsensitive)
               ? QStringLiteral("iOS")
               : (m_config.userAgent.contains(QStringLiteral("Windows"), Qt::CaseInsensitive)
                      ? QStringLiteral("Windows")
                      : (m_config.userAgent.contains(QStringLiteral("Macintosh"), Qt::CaseInsensitive)
                             ? QStringLiteral("macOS")
                             : QStringLiteral("Linux"))));
    m_interceptor = std::make_unique<CustomUrlInterceptor>(m_config.userAgent, platform);
    setupNetworkProxy();
    injectFingerprintScripts();
}

ProfileInstance::~ProfileInstance() = default;

void ProfileInstance::setupStoragePaths()
{
    m_paths = ProfileSandbox::prepare(m_config.id, m_storageRoot);
}

void ProfileInstance::synchronizeProxyGeoLocation()
{
    if (m_config.proxy.type() == QNetworkProxy::NoProxy) {
        return;
    }

    const QString exitIp = m_config.expectedProxyIp.trimmed().isEmpty()
        ? m_config.proxy.hostName().trimmed()
        : m_config.expectedProxyIp.trimmed();
    QHostAddress exitAddress;
    if (!exitAddress.setAddress(exitIp)) {
        return;
    }

    const QString configuredPath = m_config.geoDatabasePath.trimmed();
    if (configuredPath.isEmpty()) {
        return;
    }

    QString databasePath = configuredPath;
    const QFileInfo configuredInfo(configuredPath);
    if (configuredInfo.isRelative()) {
        const QStringList candidates{
            QDir::current().absoluteFilePath(configuredPath),
            QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(configuredPath),
            QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
                QStringLiteral("../") + configuredPath),
        };
        for (const QString& candidate : candidates) {
            if (QFileInfo::exists(candidate)) {
                databasePath = candidate;
                break;
            }
        }
    }
    if (!QFileInfo::exists(databasePath)) {
        return;
    }

    const QByteArray encodedPath = QFile::encodeName(databasePath);
    GeoSyncEngine geoSync(std::string(encodedPath.constData(), encodedPath.size()));
    const std::optional<GeoLocationData> location = geoSync.resolveProxyIp(exitAddress.toString());
    if (!location.has_value()) {
        return;
    }

    if (!location->countryCode.isEmpty()) {
        m_config.countryCode = location->countryCode;
    }
    if (!location->timezone.isEmpty()) {
        m_config.timezone = location->timezone;
        m_config.timezoneOffsetMinutes = location->timezoneOffsetMinutes;
    }
    if (!location->languages.isEmpty()) {
        m_config.languages = location->languages;
    }
}

void ProfileInstance::enforceWebRtcPolicy(QWebEngineSettings& settings)
{
    settings.setAttribute(QWebEngineSettings::WebRTCPublicInterfacesOnly, true);
}

void ProfileInstance::initializeWebEngineProfile()
{
    if (m_profile) {
        return;
    }
    m_profile = std::make_unique<QWebEngineProfile>(m_config.id);
    m_profile->setPersistentStoragePath(m_paths.persistentStoragePath);
    m_profile->setCachePath(m_paths.cachePath);
    m_profile->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);
    m_profile->setHttpUserAgent(m_config.userAgent);
    m_profile->setHttpAcceptLanguage(m_config.languages.join(QStringLiteral(",")));
    enforceWebRtcPolicy(*m_profile->settings());
    m_profile->settings()->setAttribute(QWebEngineSettings::DnsPrefetchEnabled, false);

    m_profile->setUrlRequestInterceptor(m_interceptor.get());
    ProfileSeedEngine seedEngine(m_config.hardware.masterSeedHex);
    FingerprintEngine::install(*m_profile, m_config, seedEngine);
}

void ProfileInstance::setupNetworkProxy()
{
    // Qt WebEngine 6.4 has no per-profile proxy setter. Applying
    // QNetworkProxy::setApplicationProxy here would leak one profile's proxy into
    // every other profile. Module 4 owns proxy process configuration and launch.
}

void ProfileInstance::injectFingerprintScripts()
{
    // Scripts are installed during lazy WebEngine initialization, before the
    // first QWebEnginePage is constructed.
}

void ProfileInstance::launch()
{
    if (m_state == State::Terminated) {
        throw std::logic_error("A terminated profile cannot be relaunched");
    }
    if (!m_page) {
        initializeWebEngineProfile();
        m_page = std::make_unique<QWebEnginePage>(m_profile.get());
        enforceWebRtcPolicy(*m_page->settings());
        m_view = std::make_unique<QWebEngineView>();
        m_view->setPage(m_page.get());
    }

    m_interceptor->setNetworkFrozen(false);
    m_state = State::Running;
    emit stateChanged(m_state);
}

void ProfileInstance::terminate()
{
    if (m_state == State::Terminated) {
        return;
    }
    m_interceptor->setNetworkFrozen(true);
    if (m_page) {
        m_page->triggerAction(QWebEnginePage::Stop);
    }
    m_view.reset();
    m_page.reset();
    m_profile.reset();
    m_state = State::Terminated;
    emit stateChanged(m_state);
}

void ProfileInstance::freezeNetworkAccess()
{
    if (m_state == State::Terminated) {
        return;
    }
    m_interceptor->setNetworkFrozen(true);
    if (m_page) {
        m_page->triggerAction(QWebEnginePage::Stop);
    }
    m_state = State::Frozen;
    emit stateChanged(m_state);
}

void ProfileInstance::unfreezeNetworkAccess()
{
    if (m_state == State::Terminated) {
        return;
    }
    m_interceptor->setNetworkFrozen(false);
    m_state = m_page ? State::Running : State::Ready;
    emit stateChanged(m_state);
}

QWebEngineView* ProfileInstance::view() const noexcept
{
    return m_view.get();
}

QWebEngineProfile* ProfileInstance::webEngineProfile() const noexcept
{
    return m_profile.get();
}

CustomUrlInterceptor* ProfileInstance::urlInterceptor() const noexcept
{
    return m_interceptor.get();
}

const ProfileConfig& ProfileInstance::config() const noexcept
{
    return m_config;
}

const ProfileSandboxPaths& ProfileInstance::sandboxPaths() const noexcept
{
    return m_paths;
}

ProfileInstance::State ProfileInstance::state() const noexcept
{
    return m_state;
}
