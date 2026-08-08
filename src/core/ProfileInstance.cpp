#include "core/ProfileInstance.hpp"

#include "core/CustomUrlInterceptor.hpp"
#include "core/ProfileValidator.hpp"

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
    const ValidationResult validation = ProfileValidator::validateProfile(config);
    if (!validation.isValid) {
        throw std::invalid_argument(validation.discrepancies.join(QStringLiteral(" ")).toStdString());
    }

    setupStoragePaths();
    m_interceptor = std::make_unique<CustomUrlInterceptor>();
    setupNetworkProxy();
    injectFingerprintScripts();
}

ProfileInstance::~ProfileInstance() = default;

void ProfileInstance::setupStoragePaths()
{
    m_paths = ProfileSandbox::prepare(m_config.id, m_storageRoot);
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

    m_profile->setUrlRequestInterceptor(m_interceptor.get());
}

void ProfileInstance::setupNetworkProxy()
{
    // Qt WebEngine 6.4 has no per-profile proxy setter. Applying
    // QNetworkProxy::setApplicationProxy here would leak one profile's proxy into
    // every other profile. Module 4 owns proxy process configuration and launch.
}

void ProfileInstance::injectFingerprintScripts()
{
    // Module 3 installs deterministic scripts before the first page is created.
}

void ProfileInstance::launch()
{
    if (m_state == State::Terminated) {
        throw std::logic_error("A terminated profile cannot be relaunched");
    }
    if (!m_page) {
        initializeWebEngineProfile();
        m_page = std::make_unique<QWebEnginePage>(m_profile.get());
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
