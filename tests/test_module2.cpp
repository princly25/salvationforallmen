#include "crypto/ProfileSeedEngine.hpp"
#include "geo/GeoSyncEngine.hpp"

#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <stdexcept>

class Module2Test final : public QObject {
    Q_OBJECT

private slots:
    void seedIsReproducibleAcrossInstances();
    void saltsProduceIndependentStreams();
    void floatsStayWithinRequestedRange();
    void invalidSeedsAndBoundsAreRejected();
    void geoDatabaseOpenFailureIsPortable();
};

void Module2Test::seedIsReproducibleAcrossInstances()
{
    const std::string masterSeed(64, '4');
    const ProfileSeedEngine firstSession(masterSeed);
    const ProfileSeedEngine secondSession(masterSeed);

    QCOMPARE(firstSession.deriveSeed("canvas_noise"), secondSession.deriveSeed("canvas_noise"));
    QCOMPARE(firstSession.deriveSeed("audio_noise"), secondSession.deriveSeed("audio_noise"));
    QCOMPARE(firstSession.deriveFloat("timezone_jitter", -1.0, 1.0),
             secondSession.deriveFloat("timezone_jitter", -1.0, 1.0));
}

void Module2Test::saltsProduceIndependentStreams()
{
    const ProfileSeedEngine engine(std::string(64, 'a'));
    QVERIFY(engine.deriveSeed("canvas_noise") != engine.deriveSeed("audio_noise"));

    const ProfileSeedEngine differentProfile(std::string(64, 'b'));
    QVERIFY(engine.deriveSeed("canvas_noise") != differentProfile.deriveSeed("canvas_noise"));
}

void Module2Test::floatsStayWithinRequestedRange()
{
    const ProfileSeedEngine engine(std::string(64, 'c'));
    for (int index = 0; index < 100; ++index) {
        const double value = engine.deriveFloat("range-" + std::to_string(index), -0.25, 0.75);
        QVERIFY(std::isfinite(value));
        QVERIFY(value >= -0.25);
        QVERIFY(value <= 0.75);
    }
    QCOMPARE(engine.deriveFloat("constant", 7.0, 7.0), 7.0);
}

void Module2Test::invalidSeedsAndBoundsAreRejected()
{
    QVERIFY_EXCEPTION_THROWN(ProfileSeedEngine("short"), std::invalid_argument);
    QVERIFY_EXCEPTION_THROWN(ProfileSeedEngine(std::string(64, 'z')), std::invalid_argument);

    const ProfileSeedEngine engine(std::string(64, 'd'));
    QVERIFY_EXCEPTION_THROWN((void)engine.deriveFloat("bad", 2.0, 1.0), std::invalid_argument);
}

void Module2Test::geoDatabaseOpenFailureIsPortable()
{
    QTemporaryDir temporaryDirectory;
    const std::string absentDatabase =
        temporaryDirectory.filePath(QStringLiteral("missing.mmdb")).toStdString();
    GeoSyncEngine engine(absentDatabase);

    QVERIFY(!engine.isOpen());
    QVERIFY(!engine.lastError().isEmpty());
    QVERIFY(!engine.resolveProxyIp(QStringLiteral("203.0.113.10")).has_value());
    QCOMPARE(engine.lastError(), QStringLiteral("GeoIP database is not open"));
}

QTEST_APPLESS_MAIN(Module2Test)
#include "test_module2.moc"
