#pragma once

#include <QElapsedTimer>
#include <QNetworkInformation>
#include <QNetworkProxy>
#include <QObject>
#include <QTcpSocket>
#include <QTimer>

#include <atomic>

enum class NetworkStatus {
    Healthy,
    ProxyDegraded,
    HardwareDisconnected,
    LeakedRisk
};

Q_DECLARE_METATYPE(NetworkStatus)

class NetworkMonitor final : public QObject {
    Q_OBJECT

public:
    explicit NetworkMonitor(const QNetworkProxy& targetProxy, QObject* parent = nullptr);

    void startMonitoring(int heartbeatIntervalMs = 50);
    void stopMonitoring();

    [[nodiscard]] bool isMonitoring() const noexcept;
    [[nodiscard]] NetworkStatus status() const noexcept;
    [[nodiscard]] int consecutiveFailures() const noexcept;
    [[nodiscard]] int heartbeatInterval() const noexcept;

signals:
    void networkEmergencyTriggered(NetworkStatus status);
    void networkRestored();
    void heartbeatSucceeded(int latencyMs);

private slots:
    void checkProxyHeartbeat();
    void onSocketConnected();
    void onSocketError(QAbstractSocket::SocketError error);
    void onAttemptTimeout();
    void onOsReachabilityChanged(QNetworkInformation::Reachability reachability);

private:
    void recordSuccess();
    void recordFailure(NetworkStatus failureStatus = NetworkStatus::ProxyDegraded);
    void abortAttempt() noexcept;
    bool proxyIsUsable() const noexcept;

    QNetworkProxy m_proxy;
    QElapsedTimer m_attemptTimer;
    QTimer m_heartbeatTimer;
    QTimer m_attemptTimeout;
    QTcpSocket m_socket;
    QMetaObject::Connection m_reachabilityConnection;
    std::atomic_bool m_isOnline{true};
    bool m_monitoring{false};
    bool m_attemptInFlight{false};
    bool m_emergencyRaised{false};
    int m_consecutiveFailures{0};
    int m_heartbeatIntervalMs{50};
    NetworkStatus m_status{NetworkStatus::Healthy};
};
