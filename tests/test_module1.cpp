#include "core/CustomUrlInterceptor.hpp"
#include "core/ProfileInstance.hpp"
#include "core/ProfileManager.hpp"
#include "core/ProfileSandbox.hpp"
#include "core/ProfileValidator.hpp"

#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {
ProfileConfig validConfig(const QString& id)
{
    ProfileConfig config;
    config.id = id;
    config.name = QStringLiteral("Test Profile");
    config.userAgent = QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
    config.hardware.masterSeedHex = std::string(64, 'a');
    return config;
}
}

class Module1Test final : public QObject {
    Q_OBJECT

private slots:
    void validatorAcceptsCoherentProfile();
    void validatorRejectsUnsafeAndIncoherentProfile();
    void sandboxCreatesDistinctStorageTrees();
    void profilePlansWebEngineStorageWithoutLaunchingChromium();
    void freezeBlocksRequestsImmediately();
    void managerRejectsDuplicateIds();
};

void Module1Test::validatorAcceptsCoherentProfile()
{
    const ValidationResult result = ProfileValidator::validateProfile(validConfig(QStringLiteral("profile-1")));
    QVERIFY2(result.isValid, qPrintable(result.discrepancies.join('\n')));
}

void Module1Test::validatorRejectsUnsafeAndIncoherentProfile()
{
    ProfileConfig config = validConfig(QStringLiteral("../escape"));
    config.hardware.masterSeedHex = "not-a-seed";
    config.hardware.webglRenderer = "Apple M3";
    config.hardware.screenWidth = 0;

    const ValidationResult result = ProfileValidator::validateProfile(config);
    QVERIFY(!result.isValid);
    QVERIFY(result.discrepancies.size() >= 4);
}

void Module1Test::sandboxCreatesDistinctStorageTrees()
{
    QTemporaryDir temporaryRoot;
    QVERIFY(temporaryRoot.isValid());
    qunsetenv("QTWEBENGINE_DICTIONARIES_PATH");

    const ProfileSandboxPaths first =
        ProfileSandbox::prepare(QStringLiteral("first"), temporaryRoot.path());
    const ProfileSandboxPaths second =
        ProfileSandbox::prepare(QStringLiteral("second"), temporaryRoot.path());

    QVERIFY(first.isComplete());
    QVERIFY(second.isComplete());
    QVERIFY(first.persistentStoragePath != second.persistentStoragePath);
    QVERIFY(first.cachePath != second.cachePath);
    QVERIFY(QDir(first.persistentStoragePath).exists());
    QVERIFY(QDir(second.persistentStoragePath).exists());
    QCOMPARE(QString::fromUtf8(qgetenv("QTWEBENGINE_DICTIONARIES_PATH")), first.dictionariesPath);
}

void Module1Test::profilePlansWebEngineStorageWithoutLaunchingChromium()
{
    QTemporaryDir temporaryRoot;
    QVERIFY(temporaryRoot.isValid());
    ProfileInstance profile(validConfig(QStringLiteral("webengine-profile")), temporaryRoot.path());

    // Chromium is initialized lazily so core tests remain compatible with
    // restricted containers whose seccomp policy rejects Chromium shutdown.
    QVERIFY(profile.webEngineProfile() == nullptr);
    QVERIFY(QDir(profile.sandboxPaths().persistentStoragePath).exists());
    QVERIFY(QDir(profile.sandboxPaths().cachePath).exists());
    QVERIFY(profile.sandboxPaths().persistentStoragePath.startsWith(temporaryRoot.path()));
    QVERIFY(profile.sandboxPaths().cachePath.startsWith(temporaryRoot.path()));
    QVERIFY(profile.sandboxPaths().persistentStoragePath != profile.sandboxPaths().cachePath);
}

void Module1Test::freezeBlocksRequestsImmediately()
{
    QTemporaryDir temporaryRoot;
    ProfileInstance profile(validConfig(QStringLiteral("freeze-profile")), temporaryRoot.path());
    QSignalSpy stateSpy(&profile, &ProfileInstance::stateChanged);

    QVERIFY(!profile.urlInterceptor()->shouldBlock(QUrl(QStringLiteral("https://example.test"))));
    profile.freezeNetworkAccess();
    QCOMPARE(profile.state(), ProfileInstance::State::Frozen);
    QVERIFY(profile.urlInterceptor()->shouldBlock(QUrl(QStringLiteral("https://example.test"))));
    QCOMPARE(stateSpy.count(), 1);

    profile.unfreezeNetworkAccess();
    QCOMPARE(profile.state(), ProfileInstance::State::Ready);
    QVERIFY(!profile.urlInterceptor()->shouldBlock(QUrl(QStringLiteral("https://example.test"))));
    QVERIFY(profile.urlInterceptor()->shouldBlock(QUrl::fromLocalFile(QStringLiteral("/tmp/secret"))));
}

void Module1Test::managerRejectsDuplicateIds()
{
    QTemporaryDir temporaryRoot;
    ProfileManager manager(temporaryRoot.path());
    manager.createProfile(validConfig(QStringLiteral("managed")));
    QCOMPARE(manager.profileCount(), 1);
    QVERIFY_EXCEPTION_THROWN(manager.createProfile(validConfig(QStringLiteral("managed"))),
                             std::invalid_argument);
    QVERIFY(manager.removeProfile(QStringLiteral("managed")));
    QCOMPARE(manager.profileCount(), 0);
}

QTEST_MAIN(Module1Test)
#include "test_module1.moc"
