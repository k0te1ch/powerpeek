#include "ui/Theme.h"

#include <winrt/Windows.UI.ViewManagement.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <unordered_map>

#include "core/Logger.h"
#include "platform/Platform.h"
#include "ui/Graphics.h"

namespace peek::ui {
namespace {

constexpr std::size_t kTypeStyleCount = 8;
constexpr int kAccentShadeCount = 7;

// 0xAARRGGBB with straight (non-premultiplied) alpha, exactly as WinUI stores its tokens.
constexpr D2D1_COLOR_F argb(std::uint32_t value) {
    return D2D1_COLOR_F{((value >> 16) & 0xFF) / 255.0f, ((value >> 8) & 0xFF) / 255.0f,
                        (value & 0xFF) / 255.0f, ((value >> 24) & 0xFF) / 255.0f};
}

constexpr D2D1_COLOR_F withAlpha(D2D1_COLOR_F color, float alpha) {
    return D2D1_COLOR_F{color.r, color.g, color.b, alpha};
}

struct Hsv {
    float h;  // degrees, [0, 360)
    float s;
    float v;
};

Hsv toHsv(D2D1_COLOR_F c) {
    float const max = std::max({c.r, c.g, c.b});
    float const min = std::min({c.r, c.g, c.b});
    float const delta = max - min;

    float hue = 0.0f;
    if (delta > 0.0f) {
        if (max == c.r) {
            hue = 60.0f * std::fmod((c.g - c.b) / delta, 6.0f);
        } else if (max == c.g) {
            hue = 60.0f * ((c.b - c.r) / delta + 2.0f);
        } else {
            hue = 60.0f * ((c.r - c.g) / delta + 4.0f);
        }
    }
    if (hue < 0.0f) {
        hue += 360.0f;
    }
    return Hsv{hue, max > 0.0f ? delta / max : 0.0f, max};
}

D2D1_COLOR_F fromHsv(Hsv hsv) {
    float const c = hsv.v * hsv.s;
    float const x = c * (1.0f - std::fabs(std::fmod(hsv.h / 60.0f, 2.0f) - 1.0f));
    float const m = hsv.v - c;

    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    if (hsv.h < 60.0f) {
        r = c;
        g = x;
    } else if (hsv.h < 120.0f) {
        r = x;
        g = c;
    } else if (hsv.h < 180.0f) {
        g = c;
        b = x;
    } else if (hsv.h < 240.0f) {
        g = x;
        b = c;
    } else if (hsv.h < 300.0f) {
        r = x;
        b = c;
    } else {
        r = c;
        b = x;
    }
    return D2D1_COLOR_F{r + m, g + m, b + m, 1.0f};
}

using AccentRamp = std::array<D2D1_COLOR_F, kAccentShadeCount>;  // dark3 .. base .. light3

bool sameRamp(AccentRamp const& a, AccentRamp const& b) {
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].r != b[i].r || a[i].g != b[i].g || a[i].b != b[i].b) {
            return false;
        }
    }
    return true;
}

// Microsoft has never published how Windows derives the six shades around the accent, and the
// measured ramp on a real machine only fits this to a few 8-bit levels. It is a last resort
// behind UISettings and the registry blob, both of which hand over the real values.
AccentRamp approximateRamp(D2D1_COLOR_F accent) {
    Hsv const base = toHsv(accent);
    auto shade = [&](float valueMultiplier, float towardsWhite, float saturationMultiplier) {
        Hsv out = base;
        out.s = base.s * saturationMultiplier;
        out.v = towardsWhite > 0.0f ? base.v + (1.0f - base.v) * towardsWhite
                                    : base.v * valueMultiplier;
        out.v = std::min(out.v, 1.0f);
        return fromHsv(out);
    };

    AccentRamp ramp{};
    ramp[0] = shade(0.3684f, 0.0f, 1.0f);
    ramp[1] = shade(0.5197f, 0.0f, 1.0f);
    ramp[2] = shade(0.7237f, 0.0f, 1.0f);
    ramp[3] = accent;
    ramp[4] = shade(0.0f, 0.7178f, 0.7246f);
    ramp[5] = shade(0.0f, 0.8932f, 0.4683f);
    ramp[6] = shade(0.0f, 1.0000f, 0.3280f);
    return ramp;
}

bool readRampFromUISettings(AccentRamp& ramp) {
    using namespace winrt::Windows::UI::ViewManagement;
    auto convert = [](winrt::Windows::UI::Color c) {
        return D2D1_COLOR_F{c.R / 255.0f, c.G / 255.0f, c.B / 255.0f, 1.0f};
    };
    try {
        UISettings settings;
        ramp[0] = convert(settings.GetColorValue(UIColorType::AccentDark3));
        ramp[1] = convert(settings.GetColorValue(UIColorType::AccentDark2));
        ramp[2] = convert(settings.GetColorValue(UIColorType::AccentDark1));
        ramp[3] = convert(settings.GetColorValue(UIColorType::Accent));
        ramp[4] = convert(settings.GetColorValue(UIColorType::AccentLight1));
        ramp[5] = convert(settings.GetColorValue(UIColorType::AccentLight2));
        ramp[6] = convert(settings.GetColorValue(UIColorType::AccentLight3));
        return true;
    } catch (winrt::hresult_error const& error) {
        log::debug(L"UISettings accent unavailable: {}", describeHresult(error.code()));
        return false;
    }
}

bool readRampFromRegistry(AccentRamp& ramp) {
    // Eight BGRA entries, light3 first. The alpha byte is zero for the ramp entries, so it is
    // ignored rather than believed.
    BYTE blob[32]{};
    DWORD size = sizeof(blob);
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent",
                     L"AccentPalette", RRF_RT_REG_BINARY, nullptr, blob, &size) != ERROR_SUCCESS ||
        size < 28) {
        return false;
    }

    auto entry = [&](int index) {
        return D2D1_COLOR_F{blob[index * 4 + 2] / 255.0f, blob[index * 4 + 1] / 255.0f,
                            blob[index * 4 + 0] / 255.0f, 1.0f};
    };
    for (int i = 0; i < kAccentShadeCount; ++i) {
        ramp[static_cast<std::size_t>(kAccentShadeCount - 1 - i)] = entry(i);
    }
    return true;
}

AccentRamp readAccentRamp() {
    AccentRamp ramp{};
    if (readRampFromUISettings(ramp) || readRampFromRegistry(ramp)) {
        return ramp;
    }
    log::warning(L"No accent palette from the system; deriving the shades from the accent colour");
    return approximateRamp(platform::systemAccentColor());
}

Palette buildPalette(bool dark, AccentRamp const& accent) {
    auto pick = [dark](std::uint32_t light, std::uint32_t night) { return argb(dark ? night : light); };

    Palette p{};
    p.windowBackground = pick(0xFFF3F3F3, 0xFF202020);
    p.layerFill = pick(0x80FFFFFF, 0x4C3A3A3A);
    p.cardFill = pick(0xB3FFFFFF, 0x0DFFFFFF);
    p.cardFillSecondary = pick(0x80F6F6F6, 0x08FFFFFF);
    // Windows 10 has no backdrop material, and WinUI's own documented fallback for acrylic is
    // a solid fill; tertiary rather than base so a flyout still reads as raised.
    p.flyoutBackground = pick(0xFFF9F9F9, 0xFF282828);

    p.controlFill = pick(0xB3FFFFFF, 0x0FFFFFFF);
    p.controlFillSecondary = pick(0x80F9F9F9, 0x15FFFFFF);
    p.controlFillTertiary = pick(0x4DF9F9F9, 0x08FFFFFF);
    p.controlFillDisabled = pick(0x4DF9F9F9, 0x0BFFFFFF);
    p.controlStrongFill = pick(0x72000000, 0x8BFFFFFF);
    p.subtleFillSecondary = pick(0x09000000, 0x0FFFFFFF);
    p.subtleFillTertiary = pick(0x06000000, 0x0AFFFFFF);

    p.cardStroke = pick(0x0F000000, 0x19000000);
    p.controlStroke = pick(0x0F000000, 0x12FFFFFF);
    p.controlStrokeSecondary = pick(0x29000000, 0x18FFFFFF);
    p.controlStrongStroke = pick(0x72000000, 0x8BFFFFFF);
    p.dividerStroke = pick(0x0F000000, 0x15FFFFFF);
    p.surfaceStroke = argb(0x66757575);  // deliberately identical in both themes
    p.focusStrokeOuter = pick(0xE4000000, 0xFFFFFFFF);
    p.focusStrokeInner = pick(0xB3FFFFFF, 0xB3000000);

    // Primary text is 89% black in light theme, never pure black; pure black is the fastest
    // way to make a Fluent-styled window look like an imitation.
    p.textPrimary = pick(0xE4000000, 0xFFFFFFFF);
    p.textSecondary = pick(0x9E000000, 0xC5FFFFFF);
    p.textTertiary = pick(0x72000000, 0x87FFFFFF);
    p.textDisabled = pick(0x5C000000, 0x5DFFFFFF);
    // Black on accent in dark theme, because there the accent fill is a light shade.
    p.textOnAccent = pick(0xFFFFFFFF, 0xFF000000);

    p.accent = dark ? accent[5] : accent[2];
    p.accentSecondary = withAlpha(p.accent, 0.9f);
    p.accentTertiary = withAlpha(p.accent, 0.8f);

    p.success = pick(0xFF0F7B0F, 0xFF6CCB5F);
    p.caution = pick(0xFF9D5D00, 0xFFFCE100);
    p.critical = pick(0xFFC42B1C, 0xFFFF99A4);
    p.successBackground = pick(0xFFDFF6DD, 0xFF393D1B);
    p.cautionBackground = pick(0xFFFFF4CE, 0xFF433519);
    p.criticalBackground = pick(0xFFFDE7E9, 0xFF442726);

    // A dark shadow needs far more alpha to read against a dark desktop than a light one does.
    p.shadow = dark ? D2D1_COLOR_F{0.0f, 0.0f, 0.0f, 0.55f}
                    : D2D1_COLOR_F{0.0f, 0.0f, 0.0f, 0.28f};
    return p;
}

struct TypeSpec {
    float size;
    float lineHeight;
    DWRITE_FONT_WEIGHT weight;
};

constexpr std::array<TypeSpec, kTypeStyleCount> kTypeRamp{{
    {12.0f, 16.0f, DWRITE_FONT_WEIGHT_NORMAL},      // Caption
    {14.0f, 20.0f, DWRITE_FONT_WEIGHT_NORMAL},      // Body
    {14.0f, 20.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD},   // BodyStrong
    {18.0f, 24.0f, DWRITE_FONT_WEIGHT_NORMAL},      // BodyLarge
    {20.0f, 28.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD},   // Subtitle
    {28.0f, 36.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD},   // Title
    {40.0f, 52.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD},   // TitleLarge
    {68.0f, 92.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD},   // Display
}};

bool fontFamilyExists(IDWriteFactory3& factory, wchar_t const* family) {
    com_ptr<IDWriteFontCollection> collection;
    if (FAILED(factory.GetSystemFontCollection(collection.put(), FALSE))) {
        return false;
    }
    UINT32 index = 0;
    BOOL exists = FALSE;
    return SUCCEEDED(collection->FindFamilyName(family, &index, &exists)) && exists;
}

// The ramp's line heights were designed against Segoe UI Variable, whose metrics are tighter
// than Segoe UI's: at Title and Title Large the natural box overflows the ramp value by about
// a pixel and clips ascenders. Reading the real metrics fixes that here and self-corrects if
// Segoe UI Variable turns out to be installed.
void applyLineSpacing(IDWriteFactory3& factory,
                      IDWriteTextFormat& format,
                      wchar_t const* family,
                      TypeSpec const& spec) {
    float ascent = 1.079102f;   // Segoe UI, measured; used if the face cannot be opened
    float descent = 0.250976f;

    com_ptr<IDWriteFontCollection> collection;
    UINT32 index = 0;
    BOOL exists = FALSE;
    if (SUCCEEDED(factory.GetSystemFontCollection(collection.put(), FALSE)) &&
        SUCCEEDED(collection->FindFamilyName(family, &index, &exists)) && exists) {
        com_ptr<IDWriteFontFamily> fontFamily;
        com_ptr<IDWriteFont> font;
        if (SUCCEEDED(collection->GetFontFamily(index, fontFamily.put())) &&
            SUCCEEDED(fontFamily->GetFirstMatchingFont(spec.weight, DWRITE_FONT_STRETCH_NORMAL,
                                                       DWRITE_FONT_STYLE_NORMAL, font.put()))) {
            DWRITE_FONT_METRICS metrics{};
            font->GetMetrics(&metrics);
            float const unitsPerEm = static_cast<float>(metrics.designUnitsPerEm);
            ascent = metrics.ascent / unitsPerEm;
            descent = metrics.descent / unitsPerEm;
        }
    }

    float const natural = (ascent + descent) * spec.size;
    float const lineHeight = spec.lineHeight > natural ? spec.lineHeight : std::ceil(natural);
    format.SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, lineHeight,
                          lineHeight * (ascent / (ascent + descent)));
}

std::wstring userLocale() {
    wchar_t buffer[LOCALE_NAME_MAX_LENGTH]{};
    if (GetUserDefaultLocaleName(buffer, LOCALE_NAME_MAX_LENGTH) > 0) {
        return buffer;
    }
    return L"en-us";
}

}  // namespace

struct Theme::Impl {
    ThemePreference preference = ThemePreference::System;
    bool dark = false;
    AccentRamp accent{};
    Palette palette{};

    std::wstring uiFamily = L"Segoe UI";
    std::wstring iconFamily = L"Segoe MDL2 Assets";
    std::wstring locale = L"en-us";

    std::array<com_ptr<IDWriteTextFormat>, kTypeStyleCount> formats{};
    std::unordered_map<int, com_ptr<IDWriteTextFormat>> iconFormats;

    void resolveFonts();
    void buildFormats();
    bool resolveTheme();
    com_ptr<IDWriteTextFormat> createFormat(std::wstring const& family,
                                            float size,
                                            DWRITE_FONT_WEIGHT weight) const;
};

com_ptr<IDWriteTextFormat> Theme::Impl::createFormat(std::wstring const& family,
                                                     float size,
                                                     DWRITE_FONT_WEIGHT weight) const {
    com_ptr<IDWriteTextFormat> format;
    HRESULT hr = GraphicsDevice::instance().dwrite()->CreateTextFormat(
        family.c_str(), nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size,
        locale.c_str(), format.put());
    if (FAILED(hr)) {
        log::error(L"CreateTextFormat failed for {} at {}: {}", family, size, describeHresult(hr));
        return nullptr;
    }
    return format;
}

void Theme::Impl::resolveFonts() {
    auto& dwrite = *GraphicsDevice::instance().dwrite();

    // Windows 10 19045 ships neither of the preferred families; both are probed rather than
    // assumed so one binary looks right on Windows 11 too.
    if (fontFamilyExists(dwrite, L"Segoe UI Variable Text")) {
        uiFamily = L"Segoe UI Variable Text";
    }
    if (fontFamilyExists(dwrite, L"Segoe Fluent Icons")) {
        iconFamily = L"Segoe Fluent Icons";
    }
    log::debug(L"UI font: {}, icon font: {}", uiFamily, iconFamily);
}

void Theme::Impl::buildFormats() {
    auto& dwrite = *GraphicsDevice::instance().dwrite();

    for (std::size_t i = 0; i < kTypeStyleCount; ++i) {
        auto const& spec = kTypeRamp[i];
        auto format = createFormat(uiFamily, spec.size, spec.weight);
        if (!format) {
            continue;
        }
        applyLineSpacing(dwrite, *format, uiFamily.c_str(), spec);
        // The shared formats serve single-line labels, which is nearly everything on a settings
        // page; wrapped paragraphs override this on their own text layout.
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

        com_ptr<IDWriteInlineObject> ellipsis;
        if (SUCCEEDED(dwrite.CreateEllipsisTrimmingSign(format.get(), ellipsis.put()))) {
            DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            format->SetTrimming(&trimming, ellipsis.get());
        }
        formats[i] = std::move(format);
    }
}

bool Theme::Impl::resolveTheme() {
    bool const wanted = preference == ThemePreference::System ? !platform::systemUsesLightTheme()
                                                              : preference == ThemePreference::Dark;
    AccentRamp const ramp = readAccentRamp();

    bool const different = wanted != dark || !sameRamp(ramp, accent);
    dark = wanted;
    accent = ramp;
    palette = buildPalette(dark, accent);
    return different;
}

Theme::Theme() : m_impl(std::make_unique<Impl>()) {
    m_impl->locale = userLocale();
    m_impl->resolveFonts();
    m_impl->buildFormats();
    m_impl->resolveTheme();
}

Theme::~Theme() = default;

Theme& Theme::instance() {
    static Theme theme;
    return theme;
}

void Theme::setPreference(ThemePreference preference) {
    if (m_impl->preference == preference) {
        return;
    }
    m_impl->preference = preference;
    if (m_impl->resolveTheme()) {
        changed();
    }
}

void Theme::refreshFromSystem() {
    if (m_impl->resolveTheme()) {
        changed();
    }
}

bool Theme::isDark() const noexcept { return m_impl->dark; }

Palette const& Theme::colors() const noexcept { return m_impl->palette; }

D2D1_COLOR_F Theme::accentShade(int step) const {
    int const index = std::clamp(step, -3, 3) + 3;
    return m_impl->accent[static_cast<std::size_t>(index)];
}

D2D1_COLOR_F Theme::levelColor(int percent) const {
    auto const& palette = m_impl->palette;
    if (percent < 0) {
        // Nothing to colour: an unknown level is drawn in the neutral text colour, which is
        // what SystemFillColorNeutral resolves to within a rounding error.
        return palette.textTertiary;
    }

    auto const& settings = SettingsStore::instance().get();
    if (percent <= settings.criticalThresholdPercent) {
        return palette.critical;
    }
    if (percent <= settings.lowThresholdPercent) {
        return palette.caution;
    }
    return palette.success;
}

IDWriteTextFormat* Theme::textFormat(TypeStyle style) const {
    auto const index = static_cast<std::size_t>(style);
    return index < kTypeStyleCount ? m_impl->formats[index].get() : nullptr;
}

IDWriteTextFormat* Theme::iconFormat(float sizeDip) const {
    // Quarter-DIP granularity: fine enough that no requested size is visibly rounded, coarse
    // enough that an animated size does not fill the cache with near-duplicates.
    int const key = static_cast<int>(std::lround(sizeDip * 4.0f));
    if (auto const found = m_impl->iconFormats.find(key); found != m_impl->iconFormats.end()) {
        return found->second.get();
    }

    auto format = m_impl->createFormat(m_impl->iconFamily, static_cast<float>(key) / 4.0f,
                                       DWRITE_FONT_WEIGHT_NORMAL);
    if (!format) {
        return nullptr;
    }
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    return m_impl->iconFormats.emplace(key, std::move(format)).first->second.get();
}

}  // namespace peek::ui
