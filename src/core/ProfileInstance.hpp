#pragma once

#include "core/ProfileConfig.hpp"
#include "core/ProfileSandbox.hpp"

#include <QObject>

#include <memory>

class CustomUrlInterceptor;
class QWebEnginePage;
class QWebEngineProfile;
class QWebEngineView;

class ProfileInstance final : public QObject {
    Q_OBJECT

public:
    enum class State { Ready, Running, Frozen, Terminated };
    Q_ENUM(State)

    explicit ProfileInstance(const ProfileConfig& config, const QString& storageRoot = {},
                             QObject* parent = nullptr);
    ~ProfileInstance() override;

    void launch();
    void terminate();
    void freezeNetworkAccess();
    void unfreezeNetworkAccess();

    [[nodiscard]] QWebEngineView* view() const noexcept;
    [[nodiscard]] QWebEngineProfile* webEngineProfile() const noexcept;
    [[nodiscard]] CustomUrlInterceptor* urlInterceptor() const noexcept;
    [[nodiscard]] const ProfileConfig& config() const noexcept;
    [[nodiscard]] const ProfileSandboxPaths& sandboxPaths() const noexcept;
    [[nodiscard]] State state() const noexcept;

signals:
    void stateChanged(ProfileInstance::State state);

private:
    void setupStoragePaths();
    void initializeWebEngineProfile();
    void setupNetworkProxy();
    void injectFingerprintScripts();

    ProfileConfig m_config;
    QString m_storageRoot;
    ProfileSandboxPaths m_paths;
    State m_state{State::Ready};
    std::unique_ptr<QWebEngineProfile> m_profile;
    std::unique_ptr<CustomUrlInterceptor> m_interceptor;
    std::unique_ptr<QWebEnginePage> m_page;
    std::unique_ptr<QWebEngineView> m_view;
};
