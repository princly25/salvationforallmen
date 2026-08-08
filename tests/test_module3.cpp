#include "core/ProfileConfig.hpp"
#include "crypto/ProfileSeedEngine.hpp"
#include "hooks/FingerprintEngine.hpp"
#include "hooks/MinHookManager.hpp"

#include <QJSEngine>
#include <QJSValue>
#include <QTest>
#include <QWebEngineScript>

namespace {
ProfileConfig fingerprintConfig()
{
    ProfileConfig config;
    config.id = QStringLiteral("fingerprint-test");
    config.name = QStringLiteral("Fingerprint Test");
    config.userAgent = QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) Test/\"quoted\"");
    config.languages = {QStringLiteral("de-DE"), QStringLiteral("de"), QStringLiteral("en")};
    config.timezone = QStringLiteral("Europe/Berlin");
    config.timezoneOffsetMinutes = 120;
    config.hardware.cpuCores = 12;
    config.hardware.memoryGb = 32;
    config.hardware.screenWidth = 2560;
    config.hardware.screenHeight = 1440;
    config.hardware.webglVendor = "Spoofed Vendor";
    config.hardware.webglRenderer = "Spoofed Renderer";
    config.hardware.masterSeedHex = std::string(64, 'e');
    return config;
}

QString mockBrowserPrelude()
{
    return QStringLiteral(R"JS(
var navigator = {};
var screen = {};
var globalThis = this;
function WebGLRenderingContext() {}
WebGLRenderingContext.prototype.getParameter = function(parameter) { return 'original:' + parameter; };
function WebGL2RenderingContext() {}
WebGL2RenderingContext.prototype.getParameter = function(parameter) { return 'original2:' + parameter; };
function CanvasRenderingContext2D() { this.pixels = [10, 20, 30, 255, 40, 50, 60, 255]; }
CanvasRenderingContext2D.prototype.getImageData = function() {
  return { data: this.pixels.slice(), width: 2, height: 1 };
};
CanvasRenderingContext2D.prototype.putImageData = function(image) { this.lastPut = image.data.slice(); };
function HTMLCanvasElement() { this.width = 2; this.height = 1; this.context = new CanvasRenderingContext2D(); }
HTMLCanvasElement.prototype.getContext = function() { return this.context; };
HTMLCanvasElement.prototype.toDataURL = function() { return this.context.lastPut.join(','); };
function AudioBuffer() { this.samples = [0.25, 0.5, 0.75]; }
AudioBuffer.prototype.getChannelData = function() { return this.samples; };
var document = { fonts: { check: function() { return true; } } };
)JS");
}
}

class Module3Test final : public QObject {
    Q_OBJECT

private slots:
    void generatedScriptIsDeterministicAndEscaped();
    void webEngineScriptRunsAtDocumentCreation();
    void mockBrowserObservesSpoofedSurfaces();
    void minHookHasPortableLinuxStub();
};

void Module3Test::generatedScriptIsDeterministicAndEscaped()
{
    const ProfileConfig config = fingerprintConfig();
    const ProfileSeedEngine firstSeed(config.hardware.masterSeedHex);
    const ProfileSeedEngine secondSeed(config.hardware.masterSeedHex);
    const QString first = FingerprintEngine::generateInjectionScript(config, firstSeed);
    const QString second = FingerprintEngine::generateInjectionScript(config, secondSeed);

    QCOMPARE(first, second);
    QVERIFY(first.contains(QStringLiteral("Test/\\\"quoted\\\"")));
    QVERIFY(!first.contains(QStringLiteral("__CPU_CORES__")));
    QVERIFY(first.contains(QStringLiteral("CanvasRenderingContext2D.prototype.getImageData")));
    QVERIFY(first.contains(QStringLiteral("WebGL2RenderingContext")));
    QVERIFY(first.contains(QStringLiteral("AudioBuffer.prototype.getChannelData")));
}

void Module3Test::webEngineScriptRunsAtDocumentCreation()
{
    const ProfileConfig config = fingerprintConfig();
    const ProfileSeedEngine seed(config.hardware.masterSeedHex);
    const QWebEngineScript script = FingerprintEngine::buildScript(config, seed);

    QCOMPARE(script.name(), QStringLiteral("adb-fingerprint-v1"));
    QCOMPARE(script.injectionPoint(), QWebEngineScript::DocumentCreation);
    QCOMPARE(script.worldId(), static_cast<quint32>(QWebEngineScript::MainWorld));
    QVERIFY(script.runsOnSubFrames());
    QVERIFY(!script.sourceCode().isEmpty());
}

void Module3Test::mockBrowserObservesSpoofedSurfaces()
{
    const ProfileConfig config = fingerprintConfig();
    const ProfileSeedEngine seed(config.hardware.masterSeedHex);
    QJSEngine engine;
    QJSValue result = engine.evaluate(mockBrowserPrelude(), QStringLiteral("mock-browser.js"));
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    result = engine.evaluate(FingerprintEngine::generateInjectionScript(config, seed),
                             QStringLiteral("fingerprint-injection.js"));
    QVERIFY2(!result.isError(), qPrintable(result.toString()));

    QCOMPARE(engine.evaluate(QStringLiteral("navigator.hardwareConcurrency")).toInt(), 12);
    QCOMPARE(engine.evaluate(QStringLiteral("navigator.deviceMemory")).toInt(), 32);
    QVERIFY(engine.evaluate(QStringLiteral("navigator.webdriver === undefined")).toBool());
    QCOMPARE(engine.evaluate(QStringLiteral("navigator.languages.join(',')")).toString(),
             QStringLiteral("de-DE,de,en"));
    QCOMPARE(engine.evaluate(QStringLiteral("screen.width + 'x' + screen.height")).toString(),
             QStringLiteral("2560x1440"));
    QCOMPARE(engine.evaluate(QStringLiteral("(new WebGLRenderingContext()).getParameter(0x9245)"))
                 .toString(),
             QStringLiteral("Spoofed Vendor"));
    QCOMPARE(engine.evaluate(QStringLiteral("(new WebGL2RenderingContext()).getParameter(0x9246)"))
                 .toString(),
             QStringLiteral("Spoofed Renderer"));
    const double audioSample = engine.evaluate(
        QStringLiteral("(new AudioBuffer()).getChannelData(0)[0]"))
                                   .toNumber();
    QVERIFY(audioSample != 0.25);
    QCOMPARE(audioSample,
             engine.evaluate(QStringLiteral("(new AudioBuffer()).getChannelData(0)[0]")).toNumber());

    const int firstPixel = engine.evaluate(
        QStringLiteral("(new CanvasRenderingContext2D()).getImageData(0,0,2,1).data[0]"))
                               .toInt();
    const int repeatedPixel = engine.evaluate(
        QStringLiteral("(new CanvasRenderingContext2D()).getImageData(0,0,2,1).data[0]"))
                                  .toInt();
    QCOMPARE(firstPixel, repeatedPixel);
    QVERIFY(firstPixel == 10 || firstPixel == 11);
    const QString spoofedPixels = engine.evaluate(QStringLiteral(
        "(new CanvasRenderingContext2D()).getImageData(0,0,2,1).data.join(',')"))
                                      .toString();
    QVERIFY(spoofedPixels != QStringLiteral("10,20,30,255,40,50,60,255"));
    const QString dataUrlAndRestoredPixels = engine.evaluate(QStringLiteral(R"JS(
(() => {
  const canvas = new HTMLCanvasElement();
  const spoofed = canvas.toDataURL();
  return spoofed + '|' + canvas.context.lastPut.join(',');
})()
)JS"))
                                                .toString();
    const QStringList canvasStates = dataUrlAndRestoredPixels.split(QChar('|'));
    QCOMPARE(canvasStates.size(), 2);
    QVERIFY(canvasStates[0] != QStringLiteral("10,20,30,255,40,50,60,255"));
    QCOMPARE(canvasStates[1], QStringLiteral("10,20,30,255,40,50,60,255"));
    QCOMPARE(engine.evaluate(QStringLiteral("document.fonts.check('12px Comic Sans MS')")).toBool(),
             false);
    QCOMPARE(engine.evaluate(QStringLiteral("document.fonts.check('12px Arial')")).toBool(), true);
    QCOMPARE(engine.evaluate(QStringLiteral("(new Date()).getTimezoneOffset()")).toInt(), -120);
}

void Module3Test::minHookHasPortableLinuxStub()
{
#ifndef _WIN32
    QVERIFY(!MinHookManager::platformSupported());
    MinHookManager manager;
    QVERIFY(!manager.initialize(fingerprintConfig().hardware));
    QVERIFY(!manager.isActive());
    QVERIFY(manager.lastError().contains(QStringLiteral("Windows")));
#else
    QVERIFY(MinHookManager::platformSupported());
#endif
}

QTEST_GUILESS_MAIN(Module3Test)
#include "test_module3.moc"
