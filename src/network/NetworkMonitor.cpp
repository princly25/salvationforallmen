#include "network/NetworkMonitor.hpp"

#include <QHostAddress>

#include <algorithm>

NetworkMonitor::NetworkMonitor(const QNetworkProxy& targetProxy, QObject* parent)
    : QObject(parent)
    , m_proxy(targetProxy)
{
    m_socket.setProxy(QNetworkProxy::NoProxy);
    m_attemptTimeout.setSingleShot(true);

    connect(&m_heartbeatTimer, &QTimer::timeout, this, &NetworkMonitor::checkProxyHeartbeat);
    connect(&m_attemptTimeout, &QTimer::timeout, this, &NetworkMonitor::onAttemptTimeout);
    connect(&m_socket, &QTcpSocket::connected, this, &NetworkMonitor::onSocketConnected);
    connect(&m_socket, &QTcpSocket::errorOccurred, this, &NetworkMonitor::onSocketError);
}

void NetworkMonitor::startMonitoring(int heartbeatIntervalMs)
{
    stopMonitoring();
    m_heartbeatIntervalMs = std::max(10, heartbeatIntervalMs);
    m_monitoring = true;
    m_isOnline.store(true, std::memory_order_release);
    m_status = NetworkStatus::Healthy;
    m_emergencyRaised = false;
    m_consecutiveFailures = 0;

    QNetworkInformation* information = QNetworkInformation::instance();
    if (information == nullptr && QNetworkInformation::loadDefaultBackend()) {
        information = QNetworkInformation::instance();
    }
    if (information != nullptr) {
        m_reachabilityConnection = connect(information,
                                           &QNetworkInformation::reachabilityChanged,
                                           this,
                                           &NetworkMonitor::onOsReachabilityChanged);
        onOsReachabilityChanged(information->reachability());
    }

    m_heartbeatTimer.start(m_heartbeatIntervalMs);
    QMetaObject::invokeMethod(this, &NetworkMonitor::checkProxyHeartbeat, Qt::QueuedConnection);
}

void NetworkMonitor::stopMonitoring()
{
    if (m_reachabilityConnection) {
        disconnect(m_reachabilityConnection);
        m_reachabilityConnection = {};
    }
    m_monitoring = false;
    m_heartbeatTimer.stop();
    m_attemptTimeout.stop();
    abortAttempt();
}

bool NetworkMonitor::isMonitoring() const noexcept
{
    return m_monitoring;
}

NetworkStatus NetworkMonitor::status() const noexcept
{
    return m_status;
}

int NetworkMonitor::consecutiveFailures() const noexcept
{
    return m_consecutiveFailures;
}

int NetworkMonitor::heartbeatInterval() const noexcept
{
    return m_heartbeatIntervalMs;
}

bool NetworkMonitor::proxyIsUsable() const noexcept
{
    const auto type = m_proxy.type();
    return type != QNetworkProxy::NoProxy && !m_proxy.hostName().isEmpty() && m_proxy.port() != 0;
}

void NetworkMonitor::checkProxyHeartbeat()
{
    if (!m_monitoring || m_attemptInFlight) {
        return;
    }
    if (!m_isOnline.load(std::memory_order_acquire)) {
        recordFailure(NetworkStatus::HardwareDisconnected);
        return;
    }
    if (!proxyIsUsable()) {
        recordFailure(NetworkStatus::LeakedRisk);
        return;
    }

    m_attemptInFlight = true;
    m_attemptTimer.start();
    m_socket.abort();
    m_socket.connectToHost(m_proxy.hostName(), m_proxy.port());
    m_attemptTimeout.start(std::max(100, m_heartbeatIntervalMs * 3));
}

void NetworkMonitor::onSocketConnected()
{
    if (!m_attemptInFlight) {
        return;
    }
    const int latencyMs = static_cast<int>(m_attemptTimer.elapsed());
    recordSuccess();
    emit heartbeatSucceeded(latencyMs);
    abortAttempt();
}

void NetworkMonitor::onSocketError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    if (m_attemptInFlight) {
        recordFailure(NetworkStatus::ProxyDegraded);
        abortAttempt();
    }
}

void NetworkMonitor::onAttemptTimeout()
{
    if (m_attemptInFlight) {
        recordFailure(NetworkStatus::ProxyDegraded);
        abortAttempt();
    }
}

void NetworkMonitor::onOsReachabilityChanged(QNetworkInformation::Reachability reachability)
{
    const bool online = reachability != QNetworkInformation::Reachability::Disconnected;
    m_isOnline.store(online, std::memory_order_release);
    if (!online) {
        abortAttempt();
        recordFailure(NetworkStatus::HardwareDisconnected);
    }
}

void NetworkMonitor::recordSuccess()
{
    m_consecutiveFailures = 0;
    const bool wasEmergency = m_emergencyRaised;
    m_status = NetworkStatus::Healthy;
    m_emergencyRaised = false;
    if (wasEmergency) {
        emit networkRestored();
    }
}

void NetworkMonitor::recordFailure(NetworkStatus failureStatus)
{
    ++m_consecutiveFailures;
    m_status = failureStatus;
    if (!m_emergencyRaised) {
        m_emergencyRaised = true;
        emit networkEmergencyTriggered(failureStatus);
    }
}

void NetworkMonitor::abortAttempt() noexcept
{
    m_attemptTimeout.stop();
    m_attemptInFlight = false;
    if (m_socket.state() != QAbstractSocket::UnconnectedState) {
        m_socket.abort();
    }
}
