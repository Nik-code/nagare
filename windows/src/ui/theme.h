#pragma once

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>

#include "gfx/renderer.h"

namespace ui
{

/// Same values as macosx/Theme.mm, so both apps stay one palette.
struct Palette
{
    D2D1_COLOR_F surface;
    D2D1_COLOR_F field;
    D2D1_COLOR_F border;
    D2D1_COLOR_F foreground;
    D2D1_COLOR_F bright;
    D2D1_COLOR_F hover;
    D2D1_COLOR_F selected;
    D2D1_COLOR_F accent;
    D2D1_COLOR_F seed;
    D2D1_COLOR_F danger;
};

enum class ThemeMode
{
    System,
    Light,
    Dark,
};

struct TextStyle
{
    float size = 13.0F;
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
    bool display = false; // Outfit, used for the wordmark, big numbers and panel titles

    auto operator<=>(TextStyle const&) const = default;
};

enum class Trim
{
    None,
    End,
    Middle,
};

D2D1_COLOR_F hex(uint32_t rgb, float alpha = 1.0F);
D2D1_COLOR_F withAlpha(D2D1_COLOR_F color, float alpha);

class Theme
{
public:
    static Theme& get();

    void init(Renderer* renderer);
    void setMode(ThemeMode mode);
    ThemeMode mode() const { return mode_; }
    bool dark() const { return dark_; }
    Palette const& colors() const;

    /// Follows the Windows light and dark setting while the mode is System.
    void systemThemeChanged();

    ID2D1SolidColorBrush* brush(D2D1_COLOR_F color);
    IDWriteTextFormat* format(TextStyle style);

    void text(
        ID2D1DeviceContext* context,
        std::wstring_view string,
        TextStyle style,
        D2D1_RECT_F rect,
        D2D1_COLOR_F color,
        DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING,
        Trim trim = Trim::End,
        DWRITE_PARAGRAPH_ALIGNMENT vertical = DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    D2D1_SIZE_F measure(std::wstring_view string, TextStyle style, float maxWidth = 100000.0F);

    /// Decodes art cropped and scaled to exactly this many pixels, so memory follows the window, not the file.
    ID2D1Bitmap1* art(std::wstring_view name, UINT pixelWidth, UINT pixelHeight);

    /// Draws a Tabler icon. Each icon and colour is parsed once.
    void icon(ID2D1DeviceContext* context, std::wstring_view name, D2D1_RECT_F rect, D2D1_COLOR_F color);

    /// The frosted tile tint used by the header, toolbar and pills.
    GlassShape tileGlass() const;
    /// The heavier sheet used for panels floating over the list.
    GlassShape panelGlass() const;

    /// Drops everything rebuilt on demand, used when the window is hidden.
    void trim();

private:
    std::wstring middleTrimmed(std::wstring_view string, TextStyle style, float width);
    bool resolveDark() const;
    void clearThemed();

    Renderer* renderer_ = nullptr;
    ThemeMode mode_ = ThemeMode::System;
    bool dark_ = true;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<ID2D1DeviceContext5> context5_;
    ComPtr<IDWriteFontCollection1> outfit_;
    wchar_t const* bodyFamily_ = L"Segoe UI";
    ComPtr<IDWriteInlineObject> ellipsis_;
    std::map<TextStyle, ComPtr<IDWriteTextFormat>> formats_;

    struct ArtEntry
    {
        UINT width = 0;
        UINT height = 0;
        ComPtr<ID2D1Bitmap1> bitmap;
    };
    std::map<std::wstring, ArtEntry, std::less<>> art_;
    std::map<std::wstring, ComPtr<ID2D1SvgDocument>, std::less<>> icons_;
};

/// A file from assets/, as embedded in the executable. The bytes are the mapped image itself, so
/// they stay valid for the whole run, cost nothing until read and are never copied. Empty if missing.
std::span<uint8_t const> loadAsset(std::wstring_view relativePath);

} // namespace ui
