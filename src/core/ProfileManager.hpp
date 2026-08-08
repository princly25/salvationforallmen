#pragma once

#include "core/ProfileConfig.hpp"

#include <QObject>

#include <map>
#include <memory>

class ProfileInstance;

class ProfileManager final : public QObject {
    Q_OBJECT

public:
    explicit ProfileManager(QString storageRoot = {}, QObject* parent = nullptr);

    ProfileInstance& createProfile(const ProfileConfig& config);
    bool removeProfile(const QString& profileId);
    [[nodiscard]] ProfileInstance* profile(const QString& profileId) const noexcept;
    [[nodiscard]] qsizetype profileCount() const noexcept;

private:
    QString m_storageRoot;
    std::map<QString, std::unique_ptr<ProfileInstance>> m_profiles;
};

