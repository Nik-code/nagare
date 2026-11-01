#include "app/droplet.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "ui/theme.h"
#include "ui/window.h"

namespace app
{

namespace
{

// DropletView.mm
constexpr float kCornerRadius = 14.0F;
constexpr float kPadding = 14.0F;
constexpr float kPitch = 12.6F;
constexpr float kRowPitch = 10.5F;
constexpr float kFirstDotInset = 4.0F;
constexpr float kLastDotInset = 8.0F;
constexpr int kMinColumns = 4;
constexpr int kRows = engine::kDefaultDotRows;

// A quarter pixel of diameter is finer than the eye can tell apart at these sizes.
constexpr float kDiameterSteps = 4.0F;
constexpr float kLargestDot = 7.0F;
constexpr UINT kCellMargin = 2;
constexpr UINT kAtlasColumns = 8;
constexpr UINT kArtWidthStep = 64;

D2D1_COLOR_F srgb(float red, float green, float blue)
{
    return D2D1::ColorF(red, green, blue, 1.0F);
}

struct Inks
{
    D2D1_COLOR_F main;
    D2D1_COLOR_F crest;
    D2D1_COLOR_F scan;
    D2D1_COLOR_F fresh;
    D2D1_COLOR_F bad;
};

/// drawRect in DropletDotsView.
Inks inksFor(engine::TransferState state, bool dark)
{
    using engine::TransferState;

    D2D1_COLOR_F const white = srgb(1.0F, 1.0F, 1.0F);
    Inks inks{};
    inks.main = dark ? srgb(0.937F, 0.902F, 0.839F) : white;
    inks.crest = inks.main;
    inks.scan = dark ? srgb(0.561F, 0.847F, 0.8F) : srgb(0.039F, 0.192F, 0.278F);
    inks.fresh = srgb(0.965F, 0.663F, 0.545F);
    inks.bad = srgb(1.0F, 0.478F, 0.333F);

    switch (state)
    {
    case TransferState::Seeding:
        inks.main = dark ? srgb(0.941F, 0.706F, 0.58F) : srgb(0.047F, 0.373F, 0.51F);
        inks.crest = dark ? srgb(1.0F, 0.941F, 0.878F) : srgb(0.878F, 0.412F, 0.29F);
        break;
    case TransferState::Finished:
        inks.main = dark ? srgb(0.561F, 0.529F, 0.482F) : srgb(0.047F, 0.373F, 0.51F);
        break;
    case TransferState::Paused:
    case TransferState::Problem:
        inks.main = dark ? srgb(0.659F, 0.627F, 0.588F) : white;
        break;
    default:
        break;
    }
    return inks;
}

D2D1_COLOR_F const& inkColor(Inks const& inks, engine::DotInk ink)
{
    switch (ink)
    {
    case engine::DotInk::Fresh:
        return inks.fresh;
    case engine::DotInk::Crest:
        return inks.crest;
    case engine::DotInk::Scan:
        return inks.scan;
    case engine::DotInk::Bad:
        return inks.bad;
    default:
        return inks.main;
    }
}

/// FNV-1a, because NSString's hash isn't something Windows can reproduce.
uint32_t nameHash(std::wstring_view name)
{
    uint32_t hash = 2166136261U;
    for (wchar_t const character : name)
    {
        hash = (hash ^ static_cast<uint32_t>(character)) * 16777619U;
    }
    return hash;
}

} // namespace

int DropletPainter::columnsFor(float width)
{
    return std::max(kMinColumns, static_cast<int>((width - kPadding * 2.0F) / kPitch));
}

wchar_t const* DropletPainter::artFor(engine::TransferState state, std::wstring_view name)
{
    using engine::TransferState;

    switch (state)
    {
    case TransferState::Seeding:
        return L"ArtRowSeed";
    case TransferState::Paused:
    case TransferState::Problem:
        return L"ArtRowPaused";
    case TransferState::Finished:
        return L"ArtRowDone";
    case TransferState::Fetching:
    case TransferState::Verifying:
        return L"ArtRowCheck";
    default:
        return nameHash(name) % 2U == 0U ? L"ArtRowDownload1" : L"ArtRowDownload2";
    }
}

void DropletPainter::buildAtlas(ID2D1DeviceContext* context, float scale)
{
    atlasScale_ = scale;
    float const largest = kLargestDot * scale;
    sizes_ = static_cast<UINT>(std::ceil(largest * kDiameterSteps)) + 1U;
    cell_ = static_cast<UINT>(std::ceil(largest)) + kCellMargin * 2U;
    cell_ += cell_ % 2U; // an even cell puts the circle's centre on a pixel corner
    UINT const rows = (sizes_ + kAtlasColumns - 1U) / kAtlasColumns;
    UINT const atlasWidth = cell_ * kAtlasColumns;
    UINT const atlasHeight = cell_ * rows;

    std::vector<uint32_t> pixels(static_cast<size_t>(atlasWidth) * atlasHeight);
    float const centre = static_cast<float>(cell_) / 2.0F;
    for (UINT index = 0; index < sizes_; ++index)
    {
        float const radius = static_cast<float>(index) / kDiameterSteps / 2.0F;
        UINT const left = index % kAtlasColumns * cell_;
        UINT const top = index / kAtlasColumns * cell_;
        for (UINT y = 0; y < cell_; ++y)
        {
            for (UINT x = 0; x < cell_; ++x)
            {
                float const distance = std::hypot(static_cast<float>(x) + 0.5F - centre, static_cast<float>(y) + 0.5F - centre);
                float const coverage = std::clamp(radius - distance + 0.5F, 0.0F, 1.0F);
                auto const level = static_cast<uint32_t>(coverage * 255.0F + 0.5F);
                pixels[static_cast<size_t>(top + y) * atlasWidth + left + x] = level << 24 | level << 16 | level << 8 | level;
            }
        }
    }

    float const dpi = scale * 96.0F;
    D2D1_BITMAP_PROPERTIES const properties = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi, dpi);
    atlas_.Reset();
    context->CreateBitmap(D2D1::SizeU(atlasWidth, atlasHeight), pixels.data(), atlasWidth * 4U, properties, &atlas_);
}

void DropletPainter::paintDots(
    ID2D1DeviceContext* context,
    D2D1_RECT_F rect,
    engine::TransferState state,
    std::span<engine::PieceDot const> dots,
    int columns,
    ComPtr<ID2D1SpriteBatch>& batch)
{
    if (dots.empty() || columns < 1)
    {
        return;
    }
    float const scale = ui::Window::get().renderer().dpi() / 96.0F;
    if (atlas_ == nullptr || atlasScale_ != scale)
    {
        buildAtlas(context, scale);
    }

    ComPtr<ID2D1DeviceContext3> context3;
    if (atlas_ == nullptr || FAILED(context->QueryInterface(IID_PPV_ARGS(&context3))))
    {
        return;
    }
    if (batch == nullptr && FAILED(context3->CreateSpriteBatch(&batch)))
    {
        return;
    }

    D2D1_MATRIX_3X2_F transform{};
    context->GetTransform(&transform);

    float const fieldWidth = rect.right - rect.left;
    float const fieldHeight = rect.bottom - rect.top;
    float const originY = (fieldHeight - static_cast<float>(kRows - 1) * kRowPitch) / 2.0F;
    float const stepX = columns > 1 ? (fieldWidth - kPadding * 2.0F - kLastDotInset) / static_cast<float>(columns - 1) : 0.0F;
    float const half = static_cast<float>(cell_) / 2.0F / scale;

    Inks const inks = inksFor(state, ui::Theme::get().dark());

    size_t const count = dots.size();
    destinations_.resize(count);
    sources_.resize(count);
    colors_.resize(count);

    size_t written = 0;
    for (int x = 0; x < columns; ++x)
    {
        // Whole device pixels keep every dot of a size identical, which is what makes them read as a grid.
        float const absoluteX = (transform._31 + rect.left + kPadding + kFirstDotInset + static_cast<float>(x) * stepX) * scale;
        float const centreX = std::round(absoluteX) / scale - transform._31;
        for (int y = 0; y < kRows; ++y)
        {
            auto const at = static_cast<size_t>(x) * kRows + static_cast<size_t>(y);
            if (at >= count)
            {
                break;
            }
            engine::PieceDot const& dot = dots[at];
            float const absoluteY = (transform._32 + rect.top + originY + static_cast<float>(y) * kRowPitch) * scale;
            float const centreY = std::round(absoluteY) / scale - transform._32;
            auto const index = static_cast<UINT>(std::clamp(std::lround(dot.size * scale * kDiameterSteps), 1L, static_cast<long>(sizes_) - 1L));

            destinations_[written] = D2D1::RectF(centreX - half, centreY - half, centreX + half, centreY + half);
            UINT const left = index % kAtlasColumns * cell_;
            UINT const top = index / kAtlasColumns * cell_;
            sources_[written] = D2D1::RectU(left, top, left + cell_, top + cell_);
            colors_[written] = ui::withAlpha(inkColor(inks, dot.ink), dot.alpha);
            ++written;
        }
    }

    batch->Clear();
    batch->AddSprites(static_cast<UINT32>(written), destinations_.data(), sources_.data(), colors_.data());

    D2D1_ANTIALIAS_MODE const previous = context3->GetAntialiasMode();
    context3->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    context3->DrawSpriteBatch(batch.Get(), atlas_.Get(), D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    context3->SetAntialiasMode(previous);
}

void DropletPainter::paint(
    ID2D1DeviceContext* context,
    D2D1_RECT_F rect,
    std::wstring_view art,
    engine::TransferState state,
    std::span<engine::PieceDot const> dots,
    int columns,
    ComPtr<ID2D1SpriteBatch>& batch)
{
    float const scale = ui::Window::get().renderer().dpi() / 96.0F;
    float const fieldWidth = rect.right - rect.left;

    // Rounding the width up means a resize re-decodes art only now and then, not on every step.
    auto const wanted = static_cast<UINT>(std::ceil(fieldWidth * scale));
    UINT const pixelWidth = (wanted + kArtWidthStep - 1U) / kArtWidthStep * kArtWidthStep;
    auto const pixelHeight = static_cast<UINT>(std::ceil((rect.bottom - rect.top) * scale));

    if (ID2D1Bitmap1* bitmap = ui::Theme::get().art(art, pixelWidth, pixelHeight); bitmap != nullptr)
    {
        if (artBrush_ == nullptr)
        {
            context->CreateBitmapBrush(bitmap, &artBrush_);
            artBrush_->SetExtendModeX(D2D1_EXTEND_MODE_CLAMP);
            artBrush_->SetExtendModeY(D2D1_EXTEND_MODE_CLAMP);
        }
        artBrush_->SetBitmap(bitmap);
        float const overhang = (static_cast<float>(pixelWidth) / scale - fieldWidth) / 2.0F;
        artBrush_->SetTransform(D2D1::Matrix3x2F::Translation(rect.left - overhang, rect.top));
        context->FillRoundedRectangle(D2D1::RoundedRect(rect, kCornerRadius, kCornerRadius), artBrush_.Get());
    }

    paintDots(context, rect, state, dots, columns, batch);
}

} // namespace app
