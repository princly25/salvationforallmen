#pragma once

#include "core/ProfileManager.hpp"
#include "network/NetworkMonitor.hpp"

#include <QMainWindow>

#include <map>
#include <memory>

class KillSwitchEngine;
class QLabel;
class QGridLayout;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QStackedWidget;
class QTableWidget;
class QWidget;
class ProfileCardWidget;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QString storageRoot = {}, QWidget* parent = nullptr);
    ~MainWindow() override;

    ProfileInstance& addProfile(const ProfileConfig& config);
    [[nodiscard]] ProfileManager& profileManager() noexcept;
    [[nodiscard]] ProfileCardWidget* profileCard(const QString& profileId) const noexcept;
    [[nodiscard]] KillSwitchEngine* killSwitch(const QString& profileId) const noexcept;

public slots:
    void addLogMessage(const QString& message);
    void updateNetworkStatus(NetworkStatus status, const QString& proxyIp = {}, int latencyMs = -1);

private slots:
    void showCreateProfileDialog();
    void launchProfile(const QString& profileId);
    void terminateProfile(const QString& profileId);
    void showCookieInspector(const QString& profileId);
    void exportCookies(const QString& profileId);
    void deleteProfile(const QString& profileId);
    void prepareGeoSync(const QString& profileId);
    void resolveGeoIp();

private:
    void buildUi();
    QWidget* buildProfilesPage();
    QWidget* buildProxyPage();
    QWidget* buildProxyFetcherPage();
    QWidget* buildDiagnosticsPage();
    QWidget* buildSettingsPage();
    void addProxyTableRow(const ProfileConfig& config);
    void handleContainment(const QString& profileId, bool contained, NetworkStatus status);
    [[nodiscard]] bool hasContainedProfile() const noexcept;
    static QString networkStatusName(NetworkStatus status);

    std::unique_ptr<ProfileManager> m_profileManager;
    std::map<QString, std::unique_ptr<KillSwitchEngine>> m_killSwitches;
    std::map<QString, ProfileCardWidget*> m_profileCards;
    QListWidget* m_navigation{nullptr};
    QStackedWidget* m_pages{nullptr};
    QGridLayout* m_profileGrid{nullptr};
    QLabel* m_emptyProfilesLabel{nullptr};
    QTableWidget* m_proxyTable{nullptr};
    QLineEdit* m_geoDatabasePath{nullptr};
    QLineEdit* m_geoIpAddress{nullptr};
    QLabel* m_geoResult{nullptr};
    QPlainTextEdit* m_logs{nullptr};
    QLabel* m_latencyStatus{nullptr};
    QLabel* m_proxyStatus{nullptr};
    QLabel* m_killSwitchStatus{nullptr};
};
