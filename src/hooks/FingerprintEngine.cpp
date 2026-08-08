#include "hooks/FingerprintEngine.hpp"

#include "crypto/ProfileSeedEngine.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QWebEngineProfile>
#include <QWebEngineScriptCollection>

#include <limits>

QString FingerprintEngine::javascriptLiteral(const QString& value)
{
    const QByteArray encoded = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(encoded.mid(1, encoded.size() - 2));
}

QString FingerprintEngine::javascriptStringArray(const QStringList& values)
{
    QJsonArray array;
    for (const QString& value : values) {
        array.append(value);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

FingerprintNoiseParameters
FingerprintEngine::deriveNoiseParameters(const ProfileSeedEngine& seedEngine)
{
    FingerprintNoiseParameters parameters;
    parameters.canvasSeed = seedEngine.deriveSeed("canvas_noise");
    parameters.webglSeed = seedEngine.deriveSeed("webgl_parameter_noise");
    parameters.audioSeed = seedEngine.deriveSeed("audio_frequency_noise");
    parameters.canvasBitShift = 1 + static_cast<int>(parameters.canvasSeed % 7U);
    parameters.webglParameterOffset = 1 + static_cast<int>(parameters.webglSeed % 31U);
    parameters.audioFrequencyOffset =
        seedEngine.deriveFloat("audio_frequency_offset", -0.0005, 0.0005);
    if (parameters.audioFrequencyOffset == 0.0) {
        parameters.audioFrequencyOffset = std::numeric_limits<double>::epsilon();
    }
    return parameters;
}

QString FingerprintEngine::generateInjectionScript(const ProfileConfig& config,
                                                   const ProfileSeedEngine& seedEngine)
{
    const FingerprintNoiseParameters noise = deriveNoiseParameters(seedEngine);
    QString script = QStringLiteral(R"JS(
(() => {
  'use strict';
  const marker = Symbol.for('adb.fingerprint.v1');
  if (globalThis[marker]) return;
  Object.defineProperty(globalThis, marker, { value: true });

  const defineGetter = (target, property, value) => {
    if (!target) return;
    try {
      Object.defineProperty(target, property, {
        get: () => value,
        configurable: true,
        enumerable: true
      });
    } catch (_) {}
  };

  const languages = Object.freeze(__LANGUAGES__);
  defineGetter(globalThis.navigator, 'hardwareConcurrency', __CPU_CORES__);
  defineGetter(globalThis.navigator, 'deviceMemory', __MEMORY_GB__);
  defineGetter(globalThis.navigator, 'webdriver', undefined);
  defineGetter(globalThis.navigator, 'languages', languages);
  defineGetter(globalThis.navigator, 'language', languages[0] || 'en-US');
  defineGetter(globalThis.navigator, 'userAgent', __USER_AGENT__);
  defineGetter(globalThis.navigator, 'platform', __PLATFORM__);

  defineGetter(globalThis.screen, 'width', __SCREEN_WIDTH__);
  defineGetter(globalThis.screen, 'height', __SCREEN_HEIGHT__);
  defineGetter(globalThis.screen, 'availWidth', __SCREEN_WIDTH__);
  defineGetter(globalThis.screen, 'availHeight', Math.max(1, __SCREEN_HEIGHT__ - 40));

  const patchWebGL = (constructor) => {
    if (!constructor || !constructor.prototype || typeof constructor.prototype.getParameter !== 'function') return;
    const original = constructor.prototype.getParameter;
    constructor.prototype.getParameter = function(parameter) {
      if (parameter === 0x9245) return __WEBGL_VENDOR__;
      if (parameter === 0x9246) return __WEBGL_RENDERER__;
      const value = Reflect.apply(original, this, arguments);
      if ((parameter === 0x0D33 || parameter === 0x84E8) && Number.isFinite(value)) {
        return Math.max(1, value - __WEBGL_PARAMETER_OFFSET__);
      }
      return value;
    };
  };
  patchWebGL(globalThis.WebGLRenderingContext);
  patchWebGL(globalThis.WebGL2RenderingContext);

  const canvasSeed = __CANVAS_SEED__ >>> 0;
  const canvasBitShift = __CANVAS_BIT_SHIFT__;
  const applyCanvasNoise = (image) => {
    if (!image || !image.data) return image;
    const pixelCount = Math.floor(image.data.length / 4);
    if (pixelCount < 1) return image;
    const startPixel = canvasSeed % Math.min(pixelCount, 16);
    for (let pixel = startPixel; pixel < pixelCount; pixel += 16) {
      image.data[pixel * 4] = image.data[pixel * 4] ^ canvasBitShift;
    }
    return image;
  };
  let rawCanvasGetImageData = null;
  let rawCanvasPutImageData = null;
  if (globalThis.CanvasRenderingContext2D && CanvasRenderingContext2D.prototype) {
    rawCanvasGetImageData = CanvasRenderingContext2D.prototype.getImageData;
    rawCanvasPutImageData = CanvasRenderingContext2D.prototype.putImageData;
    if (typeof rawCanvasGetImageData === 'function') {
      CanvasRenderingContext2D.prototype.getImageData = function() {
        return applyCanvasNoise(Reflect.apply(rawCanvasGetImageData, this, arguments));
      };
    }
  }

  if (globalThis.HTMLCanvasElement && HTMLCanvasElement.prototype) {
    const originalToDataURL = HTMLCanvasElement.prototype.toDataURL;
    if (typeof originalToDataURL === 'function') {
      HTMLCanvasElement.prototype.toDataURL = function() {
        const context = typeof this.getContext === 'function' ? this.getContext('2d') : null;
        if (!context || typeof rawCanvasGetImageData !== 'function'
            || typeof rawCanvasPutImageData !== 'function') {
          return Reflect.apply(originalToDataURL, this, arguments);
        }
        try {
          const image = Reflect.apply(rawCanvasGetImageData, context,
                                      [0, 0, Math.max(1, this.width), Math.max(1, this.height)]);
          applyCanvasNoise(image);
          Reflect.apply(rawCanvasPutImageData, context, [image, 0, 0]);
          try {
            return Reflect.apply(originalToDataURL, this, arguments);
          } finally {
            applyCanvasNoise(image);
            Reflect.apply(rawCanvasPutImageData, context, [image, 0, 0]);
          }
        } catch (_) {
          return Reflect.apply(originalToDataURL, this, arguments);
        }
      };
    }
  }

  const audioSeed = __AUDIO_SEED__ >>> 0;
  const audioFrequencyOffset = __AUDIO_FREQUENCY_OFFSET__;
  if (globalThis.AudioBuffer && AudioBuffer.prototype && typeof AudioBuffer.prototype.getChannelData === 'function') {
    const originalGetChannelData = AudioBuffer.prototype.getChannelData;
    const processed = new WeakSet();
    AudioBuffer.prototype.getChannelData = function() {
      const data = Reflect.apply(originalGetChannelData, this, arguments);
      if (data && !processed.has(data)) {
        const signedNoise = (audioSeed & 1) === 0 ? 1 : -1;
        const noise = signedNoise * audioFrequencyOffset;
        for (let index = 0; index < data.length; index += 100) data[index] += noise;
        processed.add(data);
      }
      return data;
    };
  }

  const allowedFonts = Object.freeze(['Arial', 'Times New Roman', 'Segoe UI', 'Courier New', 'Tahoma', 'Verdana']);
  if (globalThis.document && document.fonts && typeof document.fonts.check === 'function') {
    const originalFontCheck = document.fonts.check.bind(document.fonts);
    document.fonts.check = function(font, text) {
      const requested = String(font).toLowerCase();
      const allowed = allowedFonts.some((family) => requested.includes(family.toLowerCase()));
      return allowed ? originalFontCheck(font, text) : false;
    };
  }

  if (globalThis.Date && Date.prototype) {
    Date.prototype.getTimezoneOffset = function() { return __TIMEZONE_OFFSET__; };
  }
  if (globalThis.Intl && Intl.DateTimeFormat) {
    const OriginalDateTimeFormat = Intl.DateTimeFormat;
    Intl.DateTimeFormat = function(locales, options) {
      const formatter = new OriginalDateTimeFormat(locales, options);
      const originalResolvedOptions = formatter.resolvedOptions.bind(formatter);
      formatter.resolvedOptions = function() {
        return Object.assign({}, originalResolvedOptions(), { timeZone: __TIMEZONE__ });
      };
      return formatter;
    };
    Intl.DateTimeFormat.prototype = OriginalDateTimeFormat.prototype;
    if (typeof OriginalDateTimeFormat.supportedLocalesOf === 'function') {
      Intl.DateTimeFormat.supportedLocalesOf = OriginalDateTimeFormat.supportedLocalesOf.bind(OriginalDateTimeFormat);
    }
  }
})();
)JS");

    const QString platform = config.userAgent.contains(QStringLiteral("Windows"), Qt::CaseInsensitive)
        ? QStringLiteral("Win32")
        : (config.userAgent.contains(QStringLiteral("Macintosh"), Qt::CaseInsensitive)
               ? QStringLiteral("MacIntel")
               : QStringLiteral("Linux x86_64"));

    script.replace(QStringLiteral("__CPU_CORES__"), QString::number(config.hardware.cpuCores));
    script.replace(QStringLiteral("__MEMORY_GB__"), QString::number(config.hardware.memoryGb));
    script.replace(QStringLiteral("__SCREEN_WIDTH__"), QString::number(config.hardware.screenWidth));
    script.replace(QStringLiteral("__SCREEN_HEIGHT__"), QString::number(config.hardware.screenHeight));
    script.replace(QStringLiteral("__CANVAS_SEED__"), QString::number(noise.canvasSeed));
    script.replace(QStringLiteral("__CANVAS_BIT_SHIFT__"),
                   QString::number(noise.canvasBitShift));
    script.replace(QStringLiteral("__WEBGL_PARAMETER_OFFSET__"),
                   QString::number(noise.webglParameterOffset));
    script.replace(QStringLiteral("__AUDIO_SEED__"), QString::number(noise.audioSeed));
    script.replace(QStringLiteral("__AUDIO_FREQUENCY_OFFSET__"),
                   QString::number(noise.audioFrequencyOffset, 'g', 17));
    script.replace(QStringLiteral("__TIMEZONE_OFFSET__"),
                   QString::number(-config.timezoneOffsetMinutes));
    script.replace(QStringLiteral("__LANGUAGES__"), javascriptStringArray(config.languages));
    script.replace(QStringLiteral("__USER_AGENT__"), javascriptLiteral(config.userAgent));
    script.replace(QStringLiteral("__PLATFORM__"), javascriptLiteral(platform));
    script.replace(QStringLiteral("__WEBGL_VENDOR__"),
                   javascriptLiteral(QString::fromStdString(config.hardware.webglVendor)));
    script.replace(QStringLiteral("__WEBGL_RENDERER__"),
                   javascriptLiteral(QString::fromStdString(config.hardware.webglRenderer)));
    script.replace(QStringLiteral("__TIMEZONE__"), javascriptLiteral(config.timezone));
    return script;
}

QWebEngineScript FingerprintEngine::buildScript(const ProfileConfig& config,
                                                const ProfileSeedEngine& seedEngine)
{
    QWebEngineScript script;
    script.setName(QStringLiteral("adb-fingerprint-v1"));
    script.setInjectionPoint(QWebEngineScript::DocumentCreation);
    script.setWorldId(QWebEngineScript::MainWorld);
    script.setRunsOnSubFrames(true);
    script.setSourceCode(generateInjectionScript(config, seedEngine));
    return script;
}

void FingerprintEngine::install(QWebEngineProfile& profile, const ProfileConfig& config,
                                const ProfileSeedEngine& seedEngine)
{
    profile.scripts()->insert(buildScript(config, seedEngine));
}
