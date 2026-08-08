#include "network/KillSwitchEngine.hpp"

#include "core/ProfileInstance.hpp"

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QNetworkRequest>
#include <QPointer>
#include <QWebEngineView>

#include <algorithm>
#include <stdexcept>

KillSwitchEngine::KillSwitchEngine(ProfileInstance* profile, QObject* parent)
    : QObject(parent)
    , m_profile(profile)
{
    if (m_profile == nullptr) {
        throw std::invalid_argument("KillSwitchEngine requires a profile");
    }
    m_verificationEndpoint = m_profile->config().proxyVerificationUrl;
    m_expectedProxyIp = m_profile->config().expectedProxyIp.trimmed();
    m_monitor = std::make_unique<NetworkMonitor>(m_profile->config().proxy, this);
    connect(m_monitor.get(), &NetworkMonitor::networkEmergencyTriggered,
            this, &KillSwitchEngine::handleEmergency);
    connect(m_monitor.get(), &NetworkMonitor::networkRestored,
            this, &KillSwitchEngine::handleRestoration);
}

void KillSwitchEngine::startMonitoring(int heartbeatIntervalMs)
{
    m_monitor->startMonitoring(heartbeatIntervalMs);
}

void KillSwitchEngine::stopMonitoring()
{
    m_monitor->stopMonitoring();
}

void KillSwitchEngine::setVerificationEndpoint(const QUrl& endpoint, const QString& expectedProxyIp)
{
    m_verificationEndpoint = endpoint;
    m_expectedProxyIp = expectedProxyIp.trimmed();
}

NetworkMonitor* KillSwitchEngine::networkMonitor() const noexcept
{
    return m_monitor.get();
}

bool KillSwitchEngine::isContained() const noexcept
{
    return m_profile->state() == ProfileInstance::State::Frozen;
}

QString KillSwitchEngine::lastError() const
{
    return m_lastError;
}

void KillSwitchEngine::handleEmergency(NetworkStatus status)
{
    if (m_profile->state() == ProfileInstance::State::Terminated) {
        return;
    }
    m_lastStatus = status;
    if (m_profile->view() != nullptr && m_profile->view()->url().isValid()) {
        m_lastUrlBeforeDrop = m_profile->view()->url().toString();
    }
    m_profile->freezeNetworkAccess();
    emit containmentChanged(true, status);
}

void KillSwitchEngine::handleRestoration()
{
    if (!isContained() || m_pendingVerification != nullptr) {
        return;
    }
    const QString scheme = m_verificationEndpoint.scheme().toLower();
    if (!m_verificationEndpoint.isValid()
        || (scheme != QStringLiteral("http") && scheme != QStringLiteral("https"))) {
        rejectRestoration(QStringLiteral("A proxy IP verification endpoint is not configured."));
        return;
    }
    if (m_expectedProxyIp.isEmpty()) {
        rejectRestoration(QStringLiteral("Expected proxy exit IP is not configured."));
        return;
    }
    const QNetworkProxy proxy = m_profile->config().proxy;
    if (proxy.type() == QNetworkProxy::NoProxy || proxy.hostName().trimmed().isEmpty()
        || proxy.port() == 0) {
        rejectRestoration(QStringLiteral("Proxy verification cannot run without a valid proxy."));
        return;
    }

    QNetworkRequest request(m_verificationEndpoint);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(1500);
    m_verificationManager.setProxy(proxy);
    m_pendingVerification = m_verificationManager.get(request);
    connect(m_pendingVerification, &QNetworkReply::finished,
            this, &KillSwitchEngine::onVerificationFinished);
}

void KillSwitchEngine::onVerificationFinished()
{
    QNetworkReply* reply = m_pendingVerification;
    m_pendingVerification = nullptr;
    if (reply == nullptr) {
        return;
    }

    const QByteArray payload = reply->readAll().trimmed();
    const bool requestSucceeded = reply->error() == QNetworkReply::NoError;
    const QString requestError = reply->errorString();
    QString observedIp = QString::fromUtf8(payload).trimmed();
    if (requestSucceeded && payload.startsWith('{')) {
        const QJsonDocument document = QJsonDocument::fromJson(payload);
        if (document.isObject()) {
            observedIp = document.object().value(QStringLiteral("ip")).toString().trimmed();
        }
    }
    reply->deleteLater();

    if (!requestSucceeded) {
        rejectRestoration(QStringLiteral("Proxy IP verification failed: %1").arg(requestError));
        return;
    }
    if (!verifyProxyIpIntegrity(observedIp)) {
        rejectRestoration(QStringLiteral("Proxy returned an unexpected or local IP address."));
        return;
    }

    m_lastError.clear();
    m_profile->unfreezeNetworkAccess();
    if (!m_lastUrlBeforeDrop.isEmpty() && m_profile->view() != nullptr) {
        m_profile->view()->setUrl(QUrl(m_lastUrlBeforeDrop));
    }
    emit containmentChanged(false, NetworkStatus::Healthy);
    emit restorationSucceeded();
}

bool KillSwitchEngine::verifyProxyIpIntegrity(const QString& observedIp) const
{
    const QHostAddress address(observedIp.trimmed());
    const QHostAddress expected(m_expectedProxyIp);
    if (address.isNull() || expected.isNull() || address != expected || address.isLoopback()) {
        return false;
    }
    const auto localAddresses = QNetworkInterface::allAddresses();
    return std::none_of(localAddresses.cbegin(), localAddresses.cend(),
                        [&address](const QHostAddress& local) { return local == address; });
}

void KillSwitchEngine::rejectRestoration(const QString& reason)
{
    m_lastError = reason;
    emit restorationRejected(reason);
}
