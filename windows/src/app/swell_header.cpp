#include "app/swell_header.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

#include "app/app.h"
#include "app/transfer_row.h"
#include "ui/widgets_internal.h"

namespace app
{

namespace
{

using engine::kSwellPoints;
using ui::TextStyle;
using ui::Theme;
using ui::Window;

// SwellHeaderView.mm
constexpr float kCornerRadius = 18.0F;
constexpr float kDownCrestWidth = 2.2F;
constexpr float kUpCrestWidth = 1.6F;
constexpr DWORD kEaseMs = 900;
constexpr float kDownRest = 0.5F;
constexpr float kDownRange = 0.3F;
constexpr float kUpRest = 0.15F;
constexpr float kUpRange = 0.24F;

// Frames aim at this pace, not at a fixed wait. The clock ticks every 15.6 ms, so a plain timer lands on
// a late tick and loses frames. Each late frame shortens the next wait instead. Every frame runs on the
// CPU and the sea moves slowly, so fifteen a second is the top.
constexpr double kFrameMs = 1000.0 / 15.0;
// A small swell can't show that many steps, so it gets just enough frames to move a third of a pixel each.
constexpr double kSlowestFrameMs = 200.0;
constexpr double kFramesPerPixel = 3.0;
// History lasts ten minutes, so after a download the sea drifts for that long. Below half a pixel the
// drift can't be seen, so it waits until it adds up to something.
constexpr float kStillPixels = 0.5F;

// Above this the window's own title bar handles dragging.
constexpr float kCaptionHeight = 56.0F;
constexpr float kHeaderTop = 12.0F;

constexpr TextStyle kWordmarkStyle{ 25.0F, DWRITE_FONT_WEIGHT_MEDIUM, true };

constexpr float kTileLeft = 16.0F;
constexpr float kTileBottom = 16.0F;
constexpr float kTileHeight = 98.0F;
constexpr float kTileRadius = 22.0F;
constexpr float kTilePaddingLeft = 22.0F;
constexpr float kTilePaddingRight = 26.0F;
constexpr float kTileTitleTop = 15.0F;
constexpr float kTileBaseline = 79.0F;
constexpr float kLightTileTint = 0.48F;
constexpr uint32_t kLightStatText = 0x0b1418;
constexpr float kColumnGap = 36.0F;
constexpr float kUnitGap = 6.0F;
constexpr TextStyle kStatTitleStyle{ 13.0F, DWRITE_FONT_WEIGHT_MEDIUM, false };
constexpr TextStyle kStatNumberStyle{ 44.0F, DWRITE_FONT_WEIGHT_MEDIUM, true };
constexpr TextStyle kStatUnitStyle{ 13.0F, DWRITE_FONT_WEIGHT_NORMAL, false };

// Clear of the system's caption buttons, which fill the corner.
constexpr float kWordmarkGap = 18.0F;
constexpr float kToolbarTop = -2.0F;
constexpr float kToolbarHeight = 36.0F;
constexpr float kToolbarCell = 39.0F;
constexpr float kToolbarPadding = 2.0F;
constexpr float kToolbarButton = 32.0F;
constexpr float kToolbarIcon = 22.0F;
constexpr size_t kToolbarButtons = 4;
constexpr float kToolbarBand = 8.0F;
constexpr float kToolbarRim = 0.2F;
constexpr float kToolbarShadow = 0.1F;

constexpr UINT kArtWidthStep = 64;

ComPtr<IDWriteTextLayout> makeLayout(std::wstring_view text, TextStyle style)
{
    Theme& theme = Theme::get();
    ComPtr<IDWriteTextLayout> layout;
    Window::get().renderer().dwrite()->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()), theme.format(style), 4096.0F, 256.0F, &layout);
    if (layout != nullptr)
    {
        layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }
    return layout;
}

/// How far below a layout's top the middle of its capitals sits, which is what reads as its centre.
/// Centring the line box would drop the word low, because that box holds the descender too.
float capCentre(IDWriteTextLayout* layout)
{
    DWRITE_LINE_METRICS line{};
    UINT32 lines = 0;
    layout->GetLineMetrics(&line, 1, &lines);

    ComPtr<IDWriteFontCollection> fonts;
    layout->GetFontCollection(0, &fonts);
    std::array<wchar_t, 64> name{};
    layout->GetFontFamilyName(0, name.data(), static_cast<UINT32>(name.size()));
    UINT32 family = 0;
    BOOL exists = FALSE;
    ComPtr<IDWriteFontFamily> found;
    ComPtr<IDWriteFont> font;
    if (fonts == nullptr || FAILED(fonts->FindFamilyName(name.data(), &family, &exists)) || !exists || FAILED(fonts->GetFontFamily(family, &found)) ||
        FAILED(found->GetFirstMatchingFont(layout->GetFontWeight(), DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font)))
    {
        return line.height / 2.0F;
    }
    DWRITE_FONT_METRICS metrics{};
    font->GetMetrics(&metrics);
    float const capHeight = layout->GetFontSize() * static_cast<float>(metrics.capHeight) / static_cast<float>(metrics.designUnitsPerEm);
    return line.baseline - capHeight / 2.0F;
}

/// Text that is drawn on every frame, so its layout has to outlive the frame.
class CachedText
{
public:
    void set(std::wstring_view text, TextStyle style)
    {
        if (layout_ != nullptr && text == text_)
        {
            return;
        }
        text_ = text;
        layout_ = makeLayout(text_, style);
        DWRITE_TEXT_METRICS metrics{};
        DWRITE_LINE_METRICS line{};
        UINT32 lines = 0;
        layout_->GetMetrics(&metrics);
        layout_->GetLineMetrics(&line, 1, &lines);
        width_ = metrics.widthIncludingTrailingWhitespace;
        baseline_ = line.baseline;
    }

    [[nodiscard]] float width() const { return width_; }

    void draw(ID2D1DeviceContext* context, float left, float top, D2D1_COLOR_F color) const
    {
        context->DrawTextLayout(D2D1::Point2F(left, top), layout_.Get(), Theme::get().brush(color));
    }

    /// Sits the text on a baseline, which is how the Mac lines up the big numbers and their units.
    void drawOnBaseline(ID2D1DeviceContext* context, float left, float baseline, D2D1_COLOR_F color) const
    {
        draw(context, left, baseline - baseline_, color);
    }

private:
    std::wstring text_;
    ComPtr<IDWriteTextLayout> layout_;
    float width_ = 0.0F;
    float baseline_ = 0.0F;
};

/// Splits "16.61 MB/s" at its last space, like setSpeed in SwellHeaderView.
void splitSpeed(double bytesPerSecond, std::wstring& value, std::wstring& unit)
{
    value = speedText(bytesPerSecond);
    size_t const space = value.rfind(L' ');
    if (space == std::wstring::npos)
    {
        unit.clear();
        return;
    }
    unit = value.substr(space + 1);
    value.resize(space);
}

float downSwell(size_t i)
{
    auto const x = static_cast<float>(i);
    return 0.05F * std::sin(x * 0.72F + 0.4F) + 0.02F * std::sin(x * 1.9F + 1.0F);
}

float upSwell(size_t i)
{
    return 0.022F * std::sin(static_cast<float>(i) * 1.05F + 2.0F);
}

} // namespace

/// The glass tile with Down and Up on it. It redraws in every frame's overlay pass, so nothing here
/// lays text out unless the text changed.
class SwellHeader::Stats final : public ui::View
{
public:
    Stats()
    {
        floating_ = true;
        glass_ = Theme::get().tileGlass();
        down_.title.set(L"Down", kStatTitleStyle);
        up_.title.set(L"Up", kStatTitleStyle);
        // Drawing needs every layout to exist, and the first frame can come before the first refresh.
        apply(down_, 0.0);
        apply(up_, 0.0);
    }

    void setSpeeds(double down, double up)
    {
        bool const changed = apply(down_, down) | apply(up_, up);
        if (!changed)
        {
            return;
        }
        float const left = frame().left;
        float const bottom = frame().bottom;
        setFrame(D2D1::RectF(left, bottom - kTileHeight, left + contentWidth(), bottom));
        invalidate();
    }

    [[nodiscard]] float contentWidth() const
    {
        return kTilePaddingLeft + columnWidth(down_) + kColumnGap + columnWidth(up_) + kTilePaddingRight;
    }

    void draw(ID2D1DeviceContext* context) override
    {
        // Glass blackens dark text on the Mac, so go past the palette in the light theme.
        D2D1_COLOR_F const bright = Theme::get().dark() ? Theme::get().colors().bright : ui::hex(kLightStatText);
        float const secondLeft = kTilePaddingLeft + columnWidth(down_) + kColumnGap;
        paintColumn(context, down_, kTilePaddingLeft, bright);
        paintColumn(context, up_, secondLeft, bright);
    }

protected:
    void adjustGlass(GlassShape& shape) const override
    {
        // Read fresh, so the tile follows a theme change.
        D2D1_RECT_F const rect = shape.rect;
        shape = Theme::get().tileGlass();
        shape.rect = rect;
        shape.radius = kTileRadius;
        if (!Theme::get().dark())
        {
            // The Mac's glass frosts far more than the shared tint when it sits over deep water.
            shape.tint = ui::hex(0xf6fafb, kLightTileTint);
        }
    }

private:
    struct Column
    {
        CachedText title;
        CachedText number;
        CachedText unit;
        std::wstring shown;
    };

    static float columnWidth(Column const& column)
    {
        return std::max(column.title.width(), column.number.width() + kUnitGap + column.unit.width());
    }

    bool apply(Column& column, double speed)
    {
        splitSpeed(speed, value_, unit_);
        if (value_ + unit_ == column.shown)
        {
            return false;
        }
        column.shown = value_ + unit_;
        column.number.set(value_, kStatNumberStyle);
        column.unit.set(unit_, kStatUnitStyle);
        return true;
    }

    static void paintColumn(ID2D1DeviceContext* context, Column const& column, float left, D2D1_COLOR_F color)
    {
        column.title.draw(context, left, kTileTitleTop, color);
        column.number.drawOnBaseline(context, left, kTileBaseline, color);
        column.unit.drawOnBaseline(context, left + column.number.width() + kUnitGap, kTileBaseline, color);
    }

    Column down_;
    Column up_;
    std::wstring value_;
    std::wstring unit_;
};

static void styleToolbarGlass(GlassShape& shape)
{
    D2D1_RECT_F const rect = shape.rect;
    shape = Theme::get().tileGlass();
    shape.rect = rect;
    shape.radius = kToolbarHeight / 2.0F;
    shape.band = kToolbarBand;
    shape.rimLight = kToolbarRim;
    shape.shadow = kToolbarShadow;
}

/// The glass behind the window buttons, which the Window draws itself so Snap Layouts keeps working.
class SwellHeader::CaptionPill final : public ui::View
{
public:
    CaptionPill()
    {
        floating_ = true;
        glass_ = Theme::get().tileGlass();
    }

protected:
    void adjustGlass(GlassShape& shape) const override
    {
        styleToolbarGlass(shape);
    }
};

/// The glass pill of buttons. The Mac's header read from the window buttons outward, so this mirrors it.
class SwellHeader::Toolbar final : public ui::View
{
public:
    explicit Toolbar(SwellHeader* owner)
    {
        floating_ = true;
        glass_ = Theme::get().tileGlass();

        add(L"Dots",
            [this, owner]
            {
                if (owner->onMore)
                {
                    auto const origin = buttons_[0]->absoluteOrigin();
                    owner->onMore(D2D1::Point2F(origin.x, origin.y + kToolbarButton + 6.0F));
                }
            });
        add(L"Info", [owner] { call(owner->onInfo); });
        add(L"Link", [owner] { call(owner->onLink); });
        add(L"Search", [owner] { call(owner->onSearch); });
    }

    static float preferredWidth()
    {
        return kToolbarPadding * 2.0F + kToolbarCell * static_cast<float>(kToolbarButtons);
    }

    void layout() override
    {
        float const inset = (kToolbarHeight - kToolbarButton) / 2.0F;
        for (size_t i = 0; i < buttons_.size(); ++i)
        {
            float const left = kToolbarPadding + (kToolbarCell - kToolbarButton) / 2.0F + static_cast<float>(i) * kToolbarCell;
            buttons_[i]->setFrame(D2D1::RectF(left, inset, left + kToolbarButton, inset + kToolbarButton));
        }
    }

protected:
    void adjustGlass(GlassShape& shape) const override
    {
        styleToolbarGlass(shape);
    }

private:
    static void call(std::function<void()> const& callback)
    {
        if (callback)
        {
            callback();
        }
    }

    void add(wchar_t const* icon, std::function<void()> onClick)
    {
        auto* button = ui::View::add<ui::IconButton>(icon, std::move(onClick));
        button->setIconSize(kToolbarIcon);
        button->setRole(ui::ColorRole::Bright);
        buttons_[count_++] = button;
    }

    std::array<ui::IconButton*, kToolbarButtons> buttons_{};
    size_t count_ = 0;
};

SwellHeader::SwellHeader()
{
    interactive_ = true;
    takesFocus_ = false;
    swellSource = [](engine::SwellData& data) { return App::get().engine().swell(data); };
    watched = [] { return Window::get().active(); };
    stats_ = add<Stats>();
    toolbar_ = add<Toolbar>(this);
    captionPill_ = add<CaptionPill>();
    wordmark_ = makeLayout(L"Nagare", kWordmarkStyle);
}

SwellHeader::~SwellHeader()
{
    stopEasing();
}

void SwellHeader::stopEasing()
{
    if (timer_ != 0)
    {
        Window::get().cancel(timer_);
        timer_ = 0;
    }
}

void SwellHeader::settle()
{
    stopEasing();
    down_ = downTo_;
    up_ = upTo_;
    pathsDirty_ = true;
}

void SwellHeader::releaseArt()
{
    artBrush_.Reset();
}

void SwellHeader::layout()
{
    float const statsLeft = kTileLeft;
    float const statsBottom = height() - kTileBottom;
    stats_->setFrame(D2D1::RectF(statsLeft, statsBottom - kTileHeight, statsLeft + stats_->contentWidth(), statsBottom));

    toolbar_->setFrame(D2D1::RectF(kTileLeft, kToolbarTop, kTileLeft + Toolbar::preferredWidth(), kToolbarTop + kToolbarHeight));
    toolbar_->layout();

    auto const origin = absoluteOrigin();
    D2D1_RECT_F const caption = ui::Window::get().captionRect();
    D2D1_RECT_F const local = D2D1::RectF(caption.left - origin.x, caption.top - origin.y, caption.right - origin.x, caption.bottom - origin.y);
    captionPill_->setFrame(local);

    DWRITE_TEXT_METRICS metrics{};
    wordmark_->GetMetrics(&metrics);
    wordmarkLeft_ = local.left - kWordmarkGap - metrics.width;
    wordmarkTop_ = (local.top + local.bottom) / 2.0F - capCentre(wordmark_.Get());
    pathsDirty_ = true;
}

void SwellHeader::refresh(double downBytesPerSecond, double upBytesPerSecond)
{
    stats_->setSpeeds(downBytesPerSecond, upBytesPerSecond);

    engine::SwellData data;
    bool const moving = swellSource(data);

    // With nothing moving the sea just lies still, so there's nothing to redraw.
    if (!moving && stillDrawn_)
    {
        return;
    }
    stillDrawn_ = !moving;

    float const peak = moving ? data.peak : 0.0F;
    Curve downTo{};
    Curve upTo{};
    for (size_t i = 0; i < kSwellPoints; ++i)
    {
        float const downShare = peak > 0.0F ? data.down[i] / peak : 0.0F;
        float const upShare = peak > 0.0F ? data.up[i] / peak : 0.0F;
        // Still water rests about halfway up the tile, so the swell always clears it.
        downTo[i] = kDownRest + kDownRange * downShare + downSwell(i);
        upTo[i] = kUpRest + kUpRange * upShare + upSwell(i);
    }

    float largest = 0.0F;
    for (size_t i = 0; i < kSwellPoints; ++i)
    {
        largest = std::max({ largest, std::abs(downTo[i] - down_[i]), std::abs(upTo[i] - up_[i]) });
    }
    if (seeded_ && largest * kHeight < kStillPixels)
    {
        return;
    }

    downTo_ = downTo;
    upTo_ = upTo;
    // Nobody is watching the sea in a window at the back, so it steps instead of easing.
    if (!seeded_ || !moving || !watched())
    {
        seeded_ = true;
        settle();
        invalidate();
        return;
    }

    downFrom_ = down_;
    upFrom_ = up_;
    easeStart_ = GetTickCount64();
    frameMs_ = std::clamp(static_cast<double>(kEaseMs) / (static_cast<double>(largest * kHeight) * kFramesPerPixel), kFrameMs, kSlowestFrameMs);
    if (timer_ == 0)
    {
        frameDue_ = static_cast<double>(easeStart_);
        scheduleFrame();
    }
}

void SwellHeader::scheduleFrame()
{
    frameDue_ += frameMs_;
    auto const now = static_cast<double>(GetTickCount64());
    timer_ = Window::get().after(static_cast<DWORD>(std::ceil(std::max(1.0, frameDue_ - now))), [this] { step(); });
}

void SwellHeader::step()
{
    timer_ = 0;

    // Invalidating from here while minimised would only ever pile up frames nobody sees.
    if (!Window::get().visible())
    {
        settle();
        return;
    }

    float const t = std::min(1.0F, static_cast<float>(GetTickCount64() - easeStart_) / static_cast<float>(kEaseMs));
    for (size_t i = 0; i < kSwellPoints; ++i)
    {
        down_[i] = ui::detail::lerp(downFrom_[i], downTo_[i], t);
        up_[i] = ui::detail::lerp(upFrom_[i], upTo_[i], t);
    }
    pathsDirty_ = true;
    invalidate();
    if (t < 1.0F)
    {
        scheduleFrame();
    }
}

ComPtr<ID2D1PathGeometry> SwellHeader::path(ID2D1DeviceContext* context, Curve const& curve, bool closed) const
{
    ComPtr<ID2D1Factory> factory;
    context->GetFactory(&factory);
    ComPtr<ID2D1PathGeometry> geometry;
    factory->CreatePathGeometry(&geometry);
    ComPtr<ID2D1GeometrySink> sink;
    geometry->Open(&sink);

    float const w = width();
    float const h = height();
    std::array<D2D1_POINT_2F, kSwellPoints> points{};
    for (size_t i = 0; i < kSwellPoints; ++i)
    {
        points[i] = D2D1::Point2F(w * static_cast<float>(i) / static_cast<float>(kSwellPoints - 1), h - h * curve[i]);
    }

    sink->BeginFigure(points[0], closed ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
    for (size_t i = 0; i + 1 < kSwellPoints; ++i)
    {
        D2D1_POINT_2F const a = points[i > 0 ? i - 1 : 0];
        D2D1_POINT_2F const b = points[i];
        D2D1_POINT_2F const c = points[i + 1];
        D2D1_POINT_2F const d = points[std::min(i + 2, kSwellPoints - 1)];
        sink->AddBezier(D2D1::BezierSegment(
            D2D1::Point2F(b.x + (c.x - a.x) / 6.0F, b.y + (c.y - a.y) / 6.0F),
            D2D1::Point2F(c.x - (d.x - b.x) / 6.0F, c.y - (d.y - b.y) / 6.0F),
            c));
    }

    if (closed)
    {
        // Water never reaches the top corners, so following the tile's two bottom corners here saves a
        // layer for the rounded clip on every frame.
        D2D1_SIZE_F const corner = D2D1::SizeF(kCornerRadius, kCornerRadius);
        sink->AddLine(D2D1::Point2F(w, h - kCornerRadius));
        sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(w - kCornerRadius, h), corner, 0.0F, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
        sink->AddLine(D2D1::Point2F(kCornerRadius, h));
        sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(0.0F, h - kCornerRadius), corner, 0.0F, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    }
    sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
    sink->Close();
    return geometry;
}

void SwellHeader::buildPaths(ID2D1DeviceContext* context)
{
    downArea_ = path(context, down_, true);
    downLine_ = path(context, down_, false);
    upArea_ = path(context, up_, true);
    upLine_ = path(context, up_, false);
    pathsDirty_ = false;
}

void SwellHeader::draw(ID2D1DeviceContext* context)
{
    if (crestStroke_ == nullptr)
    {
        ComPtr<ID2D1Factory> factory;
        context->GetFactory(&factory);
        D2D1_STROKE_STYLE_PROPERTIES const properties = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
        factory->CreateStrokeStyle(properties, nullptr, 0, &crestStroke_);
    }
    if (pathsDirty_)
    {
        buildPaths(context);
    }

    Theme& theme = Theme::get();
    bool const dark = theme.dark();
    float const scale = Window::get().renderer().dpi() / 96.0F;
    D2D1_RECT_F const tile = bounds();

    // Rounding the width up means a resize re-decodes art only now and then, not on every step.
    auto const wanted = static_cast<UINT>(std::ceil(width() * scale));
    UINT const pixelWidth = (wanted + kArtWidthStep - 1U) / kArtWidthStep * kArtWidthStep;
    auto const pixelHeight = static_cast<UINT>(std::ceil(height() * scale));
    float const overhang = (static_cast<float>(pixelWidth) / scale - width()) / 2.0F;

    auto const art = [&](std::wstring_view name) -> ID2D1Brush*
    {
        ID2D1Bitmap1* bitmap = theme.art(name, pixelWidth, pixelHeight);
        if (bitmap == nullptr)
        {
            return theme.brush(theme.colors().surface);
        }
        if (artBrush_ == nullptr)
        {
            context->CreateBitmapBrush(bitmap, &artBrush_);
            artBrush_->SetExtendModeX(D2D1_EXTEND_MODE_CLAMP);
            artBrush_->SetExtendModeY(D2D1_EXTEND_MODE_CLAMP);
        }
        artBrush_->SetTransform(D2D1::Matrix3x2F::Translation(-overhang, 0.0F));
        artBrush_->SetBitmap(bitmap);
        return artBrush_.Get();
    };

    context->FillRoundedRectangle(D2D1::RoundedRect(tile, kCornerRadius, kCornerRadius), art(L"ArtSky"));

    context->PushAxisAlignedClip(tile, D2D1_ANTIALIAS_MODE_ALIASED);
    context->FillGeometry(downArea_.Get(), art(L"ArtWaterDown"));
    D2D1_COLOR_F const downCrest = dark ? D2D1::ColorF(0.659F, 0.894F, 0.855F) : D2D1::ColorF(0.91F, 0.984F, 0.965F);
    context->DrawGeometry(downLine_.Get(), theme.brush(downCrest), kDownCrestWidth, crestStroke_.Get());
    context->FillGeometry(upArea_.Get(), art(L"ArtWaterUp"));
    D2D1_COLOR_F const upCrest = dark ? D2D1::ColorF(0.941F, 0.706F, 0.58F) : D2D1::ColorF(1.0F, 0.957F, 0.902F);
    context->DrawGeometry(upLine_.Get(), theme.brush(upCrest), kUpCrestWidth, crestStroke_.Get());
    context->PopAxisAlignedClip();

    ui::detail::strokeRounded(context, tile, kCornerRadius, D2D1::ColorF(1.0F, 0.941F, 0.863F, dark ? 0.08F : 0.5F));

    context->DrawTextLayout(D2D1::Point2F(wordmarkLeft_, wordmarkTop_), wordmark_.Get(), theme.brush(theme.colors().bright));
}

bool SwellHeader::onMouse(ui::MouseEvent const& event)
{
    if (event.kind == ui::MouseKind::Down)
    {
        // Dragging through the system, like a title bar, stays under the cursor between displays.
        HWND window = Window::get().handle();
        DWORD const position = GetMessagePos();
        ReleaseCapture();
        SendMessageW(window, WM_NCLBUTTONDOWN, HTCAPTION, static_cast<LPARAM>(position));
        return true;
    }
    return event.kind == ui::MouseKind::Up;
}

ui::View* SwellHeader::hitTest(D2D1_POINT_2F point)
{
    ui::View* hit = View::hitTest(point);
    // Leave the caption strip to the window, so its double-click and snap gestures keep working.
    return hit == this && point.y + kHeaderTop < kCaptionHeight ? nullptr : hit;
}

} // namespace app
