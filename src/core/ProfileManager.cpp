#include "core/ProfileManager.hpp"

#include "core/ProfileInstance.hpp"

#include <stdexcept>

ProfileManager::ProfileManager(QString storageRoot, QObject* parent)
    : QObject(parent)
    , m_storageRoot(std::move(storageRoot))
{
}

ProfileInstance& ProfileManager::createProfile(const ProfileConfig& config)
{
    if (m_profiles.contains(config.id)) {
        throw std::invalid_argument("A profile with this id already exists");
    }

    auto instance = std::make_unique<ProfileInstance>(config, m_storageRoot);
    ProfileInstance& reference = *instance;
    m_profiles.emplace(config.id, std::move(instance));
    return reference;
}

bool ProfileManager::removeProfile(const QString& profileId)
{
    const auto found = m_profiles.find(profileId);
    if (found == m_profiles.end()) {
        return false;
    }
    found->second->terminate();
    m_profiles.erase(found);
    return true;
}

ProfileInstance* ProfileManager::profile(const QString& profileId) const noexcept
{
    const auto found = m_profiles.find(profileId);
    return found == m_profiles.end() ? nullptr : found->second.get();
}

qsizetype ProfileManager::profileCount() const noexcept
{
    return static_cast<qsizetype>(m_profiles.size());
}

