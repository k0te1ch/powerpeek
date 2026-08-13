#pragma once

#include <memory>

#include <d2d1_1.h>
#include <dwrite_3.h>

#include "core/Settings.h"
#include "core/Signal.h"
#include "core/Win.h"

namespace peek::ui {

// The Fluent colour tokens this application draws with, named after their WinUI theme
// resources so a value can be checked against the upstream resource dictionary.
struct Palette {
    D2D1_COLOR_F windowBackground;      // SolidBackgroundFillColorBase
    D2D1_COLOR_F layerFill;             // LayerFillColorDefault
    D2D1_COLOR_F cardFill;              // CardBackgroundFillColorDefault
    D2D1_COLOR_F cardFillSecondary;     // CardBackgroundFillColorSecondary
    D2D1_COLOR_F flyoutBackground;      // AcrylicBackgroundFillColorDefault, flattened

    D2D1_COLOR_F controlFill;           // ControlFillColorDefault
    D2D1_COLOR_F controlFillSecondary;  // ControlFillColorSecondary  (hover)
    D2D1_COLOR_F controlFillTertiary;   // ControlFillColorTertiary   (pressed)
    D2D1_COLOR_F controlFillDisabled;   // ControlFillColorDisabled
    D2D1_COLOR_F controlStrongFill;     // ControlStrongFillColorDefault
    D2D1_COLOR_F subtleFillSecondary;   // SubtleFillColorSecondary
    D2D1_COLOR_F subtleFillTertiary;    // SubtleFillColorTertiary

    D2D1_COLOR_F cardStroke;            // CardStrokeColorDefault
    D2D1_COLOR_F controlStroke;         // ControlStrokeColorDefault
    D2D1_COLOR_F controlStrokeSecondary;// ControlStrokeColorSecondary
    D2D1_COLOR_F controlStrongStroke;   // ControlStrongStrokeColorDefault
    D2D1_COLOR_F dividerStroke;         // DividerStrokeColorDefault
    D2D1_COLOR_F surfaceStroke;         // SurfaceStrokeColorDefault
    D2D1_COLOR_F focusStrokeOuter;      // FocusStrokeColorOuter
    D2D1_COLOR_F focusStrokeInner;      // FocusStrokeColorInner

    D2D1_COLOR_F textPrimary;           // TextFillColorPrimary
    D2D1_COLOR_F textSecondary;         // TextFillColorSecondary
    D2D1_COLOR_F textTertiary;          // TextFillColorTertiary
    D2D1_COLOR_F textDisabled;          // TextFillColorDisabled
    D2D1_COLOR_F textOnAccent;          // TextOnAccentFillColorPrimary

    D2D1_COLOR_F accent;                // AccentFillColorDefault
    D2D1_COLOR_F accentSecondary;       // AccentFillColorSecondary   (hover)
    D2D1_COLOR_F accentTertiary;        // AccentFillColorTertiary    (pressed)

    // Battery levels map onto these: healthy, getting low, critical.
    D2D1_COLOR_F success;               // SystemFillColorSuccess
    D2D1_COLOR_F caution;               // SystemFillColorCaution
    D2D1_COLOR_F critical;              // SystemFillColorCritical
    D2D1_COLOR_F successBackground;     // SystemFillColorSuccessBackground
    D2D1_COLOR_F cautionBackground;     // SystemFillColorCautionBackground
    D2D1_COLOR_F criticalBackground;    // SystemFillColorCriticalBackground

    // Colour of the window's own drop shadow, drawn into the transparent margin.
    D2D1_COLOR_F shadow;
};

// Layout constants in DIPs, matching the WinUI control specs a settings page is built
// from. Kept as one struct so the numbers are all visible in one place instead of being
// scattered as literals through the drawing code.
struct Metrics {
    static constexpr float controlCornerRadius = 4.0f;
    static constexpr float overlayCornerRadius = 8.0f;
    static constexpr float windowCornerRadius = 8.0f;

    static constexpr float controlHeight = 32.0f;
    static constexpr float settingsCardHeight = 68.0f;
    static constexpr float settingsCardPadding = 16.0f;
    static constexpr float cardGap = 4.0f;

    static constexpr float pageMarginX = 36.0f;
    static constexpr float pageMarginTop = 16.0f;
    static constexpr float titleBarHeight = 40.0f;

    static constexpr float navPaneWidth = 64.0f;
    static constexpr float navItemHeight = 40.0f;
    static constexpr float navIndicatorWidth = 3.0f;
    static constexpr float navIndicatorLength = 16.0f;

    // The transparent border the window reserves so it can draw its own shadow. Direct2D
    // truncates a Gaussian at three standard deviations, so the margin has to cover
    // 3 * (shadowBlur / 2) + shadowOffsetY or the shadow ends in a visible hard edge.
    static constexpr float shadowMargin = 24.0f;
    static constexpr float shadowBlur = 12.0f;
    static constexpr float shadowOffsetY = 4.0f;
};

// The WinUI type ramp. Sizes are in DIPs.
enum class TypeStyle {
    Caption,     // 12 / 16
    Body,        // 14 / 20
    BodyStrong,  // 14 / 20 semibold
    BodyLarge,   // 18 / 24
    Subtitle,    // 20 / 28 semibold
    Title,       // 28 / 36 semibold
    TitleLarge,  // 40 / 52 semibold
    Display,     // 68 / 92 semibold
};

// Segoe MDL2 Assets codepoints, each verified by rendering it from segmdl2.ttf rather
// than taken from a documentation table. Windows 11 ships Segoe Fluent Icons instead,
// which is a superset at these codepoints, so one table serves both.
//
// Battery levels, the gamepad body and the charging bolt in the gauges are drawn as
// Direct2D geometry instead: they have to animate and take the level colour, which a
// font glyph cannot do.
namespace glyph {
inline constexpr wchar_t kSettings[] = L"\uE713";
inline constexpr wchar_t kInfo[] = L"\uE946";
inline constexpr wchar_t kChevronRight[] = L"\uE76C";
inline constexpr wchar_t kChevronDown[] = L"\uE70D";
inline constexpr wchar_t kChevronUp[] = L"\uE70E";
inline constexpr wchar_t kClose[] = L"\uE8BB";
inline constexpr wchar_t kMinimise[] = L"\uE921";
inline constexpr wchar_t kMaximise[] = L"\uE922";
inline constexpr wchar_t kRestore[] = L"\uE923";
inline constexpr wchar_t kRefresh[] = L"\uE72C";
inline constexpr wchar_t kFolder[] = L"\uED25";
inline constexpr wchar_t kPlay[] = L"\uE768";
inline constexpr wchar_t kVolume[] = L"\uE767";
inline constexpr wchar_t kMute[] = L"\uE74F";
inline constexpr wchar_t kGamepad[] = L"\uE7FC";
inline constexpr wchar_t kBluetooth[] = L"\uE702";
inline constexpr wchar_t kUsb[] = L"\uECF0";
inline constexpr wchar_t kWireless[] = L"\uE93E";
inline constexpr wchar_t kChart[] = L"\uE9D9";
inline constexpr wchar_t kWarning[] = L"\uE7BA";
inline constexpr wchar_t kCharging[] = L"\uE945";
inline constexpr wchar_t kCompleted[] = L"\uE930";
inline constexpr wchar_t kBatteryUnknown[] = L"\uE996";
inline constexpr wchar_t kContact[] = L"\uE77B";
inline constexpr wchar_t kHeart[] = L"\uEB51";
}  // namespace glyph

// Resolves the effective theme from the user's preference and the system setting, owns
// the colour table and the DirectWrite text formats, and tells everyone when any of that
// changes so windows can invalidate.
class Theme {
public:
    static Theme& instance();

    void setPreference(ThemePreference preference);

    // Re-reads AppsUseLightTheme and the system accent; call on WM_SETTINGCHANGE with
    // lParam "ImmersiveColorSet".
    void refreshFromSystem();

    bool isDark() const noexcept;
    Palette const& colors() const noexcept;

    // Windows derives six shades around the user's accent colour; `step` runs -3 (darkest)
    // to +3 (lightest), 0 being the accent itself.
    D2D1_COLOR_F accentShade(int step) const;

    // The colour a battery level should be drawn in: success, caution or critical,
    // according to the configured thresholds.
    D2D1_COLOR_F levelColor(int percent) const;

    IDWriteTextFormat* textFormat(TypeStyle style) const;

    // Segoe MDL2 Assets (or Segoe Fluent Icons where present) at an arbitrary size.
    IDWriteTextFormat* iconFormat(float sizeDip) const;

    Signal<> changed;

private:
    Theme();
    ~Theme();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// Convenience shorthand; the drawing code reads better as theme().colors().textPrimary.
inline Theme& theme() { return Theme::instance(); }

}  // namespace peek::ui
