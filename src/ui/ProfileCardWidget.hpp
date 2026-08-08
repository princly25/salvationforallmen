#pragma once

#include "core/ProfileInstance.hpp"

#include <QFrame>

#include <string>

class QLabel;
class QPushButton;

class ProfileCardWidget final : public QFrame {
    Q_OBJECT

public:
    explicit ProfileCardWidget(ProfileInstance* profile, QWidget* parent = nullptr);

    [[nodiscard]] ProfileInstance* profile() const noexcept;
    void setProxyStatus(bool reachable, int latencyMs = -1, const QString& location = {});
    void setCookieCount(int count);

signals:
    void launchRequested(const QString& profileId);
    void terminateRequested(const QString& profileId);
    void cookieInspectorRequested(const QString& profileId);
    void geoSyncRequested(const QString& profileId);
    void exportCookiesRequested(const QString& profileId);
    void deleteRequested(const QString& profileId);

private slots:
    void refreshState(ProfileInstance::State state);
    void toggleFreeze();

private:
    static QString stateName(ProfileInstance::State state);
    static QString proxySummary(const QNetworkProxy& proxy);
    static QString seedSummary(const std::string& seed);
    static QString platformSummary(const QString& userAgent);

    ProfileInstance* m_profile{nullptr};
    QLabel* m_stateLabel{nullptr};
    QLabel* m_proxyStatusDot{nullptr};
    QLabel* m_proxyStatusLabel{nullptr};
    QLabel* m_proxyLocationLabel{nullptr};
    QLabel* m_seedLabel{nullptr};
    QLabel* m_cookieCountLabel{nullptr};
    QPushButton* m_launchButton{nullptr};
    QPushButton* m_freezeButton{nullptr};
    QPushButton* m_terminateButton{nullptr};
};
