#pragma once

#include "network/NetworkMonitor.hpp"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QUrl>

#include <memory>

class ProfileInstance;

class KillSwitchEngine final : public QObject {
    Q_OBJECT

public:
    explicit KillSwitchEngine(ProfileInstance* profile, QObject* parent = nullptr);

    void startMonitoring(int heartbeatIntervalMs = 50);
    void stopMonitoring();
    void setVerificationEndpoint(const QUrl& endpoint, const QString& expectedProxyIp);

    [[nodiscard]] NetworkMonitor* networkMonitor() const noexcept;
    [[nodiscard]] bool isContained() const noexcept;
    [[nodiscard]] QString lastError() const;

public slots:
    void handleEmergency(NetworkStatus status);
    void handleRestoration();

signals:
    void containmentChanged(bool contained, NetworkStatus status);
    void restorationRejected(const QString& reason);
    void restorationSucceeded();

private slots:
    void onVerificationFinished();

private:
    bool verifyProxyIpIntegrity(const QString& observedIp) const;
    void rejectRestoration(const QString& reason);

    ProfileInstance* m_profile{nullptr};
    std::unique_ptr<NetworkMonitor> m_monitor;
    QNetworkAccessManager m_verificationManager;
    QPointer<QNetworkReply> m_pendingVerification;
    NetworkStatus m_lastStatus{NetworkStatus::Healthy};
    QString m_lastUrlBeforeDrop;
    QUrl m_verificationEndpoint;
    QString m_expectedProxyIp;
    QString m_lastError;
};
