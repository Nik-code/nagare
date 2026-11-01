#pragma once

#include <algorithm>
#include <cmath>

#include "ui/theme.h"
#include "ui/view.h"
#include "ui/window.h"

namespace ui::detail
{

constexpr DWORD kShortAnimationMs = 150;

inline float lerp(float from, float to, float t)
{
    return from + (to - from) * t;
}

inline float easeOut(float t)
{
    float const rest = 1.0F - t;
    return 1.0F - rest * rest * rest;
}

inline D2D1_COLOR_F mix(D2D1_COLOR_F from, D2D1_COLOR_F to, float t)
{
    return D2D1::ColorF(lerp(from.r, to.r, t), lerp(from.g, to.g, t), lerp(from.b, to.b, t), lerp(from.a, to.a, t));
}

/// One physical pixel, in device-independent pixels.
inline float hairline()
{
    return 96.0F / Window::get().renderer().dpi();
}

inline float snapToPixel(float value)
{
    float const pixel = hairline();
    return std::round(value / pixel) * pixel;
}

inline D2D1_RECT_F inset(D2D1_RECT_F rect, float dx, float dy)
{
    return D2D1::RectF(rect.left + dx, rect.top + dy, rect.right - dx, rect.bottom - dy);
}

inline bool contains(D2D1_RECT_F const& rect, D2D1_POINT_2F point)
{
    return point.x >= rect.left && point.x < rect.right && point.y >= rect.top && point.y < rect.bottom;
}

inline bool isPress(MouseKind kind)
{
    // The second click of a quick pair arrives as a double click, and it is still a click.
    return kind == MouseKind::Down || kind == MouseKind::DoubleClick;
}

/// A hairline drawn half a pixel in, so it stays one crisp pixel on a rounded rectangle.
inline void strokeRounded(ID2D1DeviceContext* context, D2D1_RECT_F rect, float radius, D2D1_COLOR_F color)
{
    float const half = hairline() / 2.0F;
    auto const shape = D2D1::RoundedRect(inset(rect, half, half), radius, radius);
    context->DrawRoundedRectangle(shape, Theme::get().brush(color), hairline());
}

inline void fillRounded(ID2D1DeviceContext* context, D2D1_RECT_F rect, float radius, D2D1_COLOR_F color)
{
    context->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), Theme::get().brush(color));
}

/// The cream wash the Mac uses for its glass controls in the dark theme.
inline D2D1_COLOR_F cream(float alpha)
{
    return hex(0xfff0dc, alpha);
}

/// Fill for quiet controls that sit on glass or the surface, from PillBar.mm's selected pill.
D2D1_COLOR_F controlFill(bool hovered, bool pressed);
D2D1_COLOR_F controlHairline();

/// Direction of `drawChevron`, in the direction the point faces.
enum class Chevron
{
    Up,
    Down,
    Right,
};

void drawChevron(ID2D1DeviceContext* context, D2D1_POINT_2F centre, float size, Chevron direction, D2D1_COLOR_F color);

} // namespace ui::detail
