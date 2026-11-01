#include <algorithm>
#include <string>
#include <vector>

#include "app/app.h"
#include "app/details_common.h"
#include "ui/widgets_internal.h"

namespace app::details
{

namespace
{

using namespace ui;

constexpr float kFieldTop = 14.0F;
constexpr float kFieldHeight = 236.0F;
constexpr float kStatsGap = 20.0F;
constexpr float kFactsGap = 22.0F;
constexpr float kFactHeight = 16.0F;
constexpr float kFactPitch = 24.0F;
constexpr float kFactLabelWidth = 76.0F;
constexpr float kFactGap = 8.0F;
constexpr float kBottomPadding = 16.0F;

// DropletView.mm
constexpr float kCornerRadius = 14.0F;
constexpr float kPadding = 14.0F;
constexpr float kPitch = 12.6F;
constexpr float kRowPitch = 10.5F;
constexpr float kDotInset = 4.0F;
constexpr int kMinColumns = 4;

constexpr float kHashSize = 13.0F;

/// DropletView's art for each state.
wchar_t const* artName(engine::TransferInfo const& info)
{
    switch (info.state)
    {
    case engine::TransferState::Seeding:
        return L"ArtRowSeed";
    case engine::TransferState::Paused:
    case engine::TransferState::Problem:
        return L"ArtRowPaused";
    case engine::TransferState::Finished:
        return L"ArtRowDone";
    case engine::TransferState::Fetching:
    case engine::TransferState::Verifying:
        return L"ArtRowCheck";
    default:
        break;
    }
    uint32_t hash = 0;
    for (wchar_t const c : info.name)
    {
        hash = hash * 31U + c;
    }
    return hash % 2U == 0U ? L"ArtRowDownload1" : L"ArtRowDownload2";
}

/// The Overview's field of 20 rows. Local until the row droplet lands, then the two unify.
class DropletField : public View
{
public:
    explicit DropletField(engine::TransferId id)
        : id_(id)
    {
    }

    void update(engine::TransferInfo const& info)
    {
        bool changed = sizeDots();
        if (info.state != state_)
        {
            state_ = info.state;
            changed = true;
        }
        if (wchar_t const* wanted = artName(info); wanted != art_)
        {
            art_ = wanted;
            bitmap_.Reset();
            brush_.Reset();
            changed = true;
        }
        if (!dots_.empty() && App::get().engine().pieceDots(id_, dots_.size(), dots_.data(), engine::kOverviewDotRows, false))
        {
            changed = true;
        }
        if (changed)
        {
            invalidate();
        }
    }

    void draw(ID2D1DeviceContext* context) override
    {
        Theme& theme = Theme::get();
        prepareArt(context);
        auto const shape = D2D1::RoundedRect(bounds(), kCornerRadius, kCornerRadius);
        if (brush_ != nullptr)
        {
            context->FillRoundedRectangle(shape, brush_.Get());
        }
        else
        {
            context->FillRoundedRectangle(shape, theme.brush(detail::controlFill(false, true)));
        }
        drawDots(context);
    }

private:
    bool sizeDots()
    {
        int const fit = static_cast<int>((width() - kPadding * 2.0F) / kPitch);
        int const columns = std::max(kMinColumns, fit);
        if (columns == columns_)
        {
            return false;
        }
        columns_ = columns;
        dots_.assign(static_cast<size_t>(columns) * engine::kOverviewDotRows, engine::PieceDot{});
        return true;
    }

    /// Theme keeps one size of each picture, and the transfer rows want these same ones at theirs.
    /// Holding the bitmap here means the two sizes can't evict each other on every paint.
    void prepareArt(ID2D1DeviceContext* context)
    {
        Theme& theme = Theme::get();
        float const scale = Window::get().renderer().dpi() / 96.0F;
        auto const pixelWidth = static_cast<UINT>(width() * scale + 0.5F);
        auto const pixelHeight = static_cast<UINT>(height() * scale + 0.5F);
        if (art_ == nullptr || (bitmap_ != nullptr && dark_ == theme.dark() && pixelWidth == pixelWidth_ && pixelHeight == pixelHeight_))
        {
            return;
        }
        bitmap_ = theme.art(art_, pixelWidth, pixelHeight);
        brush_.Reset();
        dark_ = theme.dark();
        pixelWidth_ = pixelWidth;
        pixelHeight_ = pixelHeight;
        if (bitmap_ != nullptr)
        {
            context->CreateBitmapBrush(bitmap_.Get(), &brush_);
        }
    }

    void drawDots(ID2D1DeviceContext* context) const
    {
        Theme& theme = Theme::get();
        bool const dark = theme.dark();

        D2D1_COLOR_F main = dark ? D2D1::ColorF(0.937F, 0.902F, 0.839F) : hex(0xffffff);
        D2D1_COLOR_F crest = main;
        D2D1_COLOR_F const scan = dark ? D2D1::ColorF(0.561F, 0.847F, 0.8F) : D2D1::ColorF(0.039F, 0.192F, 0.278F);
        D2D1_COLOR_F const fresh = D2D1::ColorF(0.965F, 0.663F, 0.545F);
        D2D1_COLOR_F const bad = D2D1::ColorF(1.0F, 0.478F, 0.333F);

        switch (state_)
        {
        case engine::TransferState::Seeding:
            main = dark ? D2D1::ColorF(0.941F, 0.706F, 0.58F) : D2D1::ColorF(0.047F, 0.373F, 0.51F);
            crest = dark ? D2D1::ColorF(1.0F, 0.941F, 0.878F) : D2D1::ColorF(0.878F, 0.412F, 0.29F);
            break;
        case engine::TransferState::Finished:
            main = dark ? D2D1::ColorF(0.561F, 0.529F, 0.482F) : D2D1::ColorF(0.047F, 0.373F, 0.51F);
            break;
        case engine::TransferState::Paused:
        case engine::TransferState::Problem:
            main = dark ? D2D1::ColorF(0.659F, 0.627F, 0.588F) : hex(0xffffff);
            break;
        default:
            break;
        }

        int const rows = engine::kOverviewDotRows;
        float const originY = (height() - static_cast<float>(rows - 1) * kRowPitch) / 2.0F;
        float const usable = width() - kPadding * 2.0F;
        float const stepX = columns_ > 1 ? (usable - kDotInset * 2.0F) / static_cast<float>(columns_ - 1) : 0.0F;

        for (int x = 0; x < columns_; ++x)
        {
            float const centerX = kPadding + kDotInset + static_cast<float>(x) * stepX;
            for (int y = 0; y < rows; ++y)
            {
                auto const& dot = dots_[static_cast<size_t>(x) * rows + static_cast<size_t>(y)];
                D2D1_COLOR_F color = main;
                switch (dot.ink)
                {
                case engine::DotInk::Fresh:
                    color = fresh;
                    break;
                case engine::DotInk::Crest:
                    color = crest;
                    break;
                case engine::DotInk::Scan:
                    color = scan;
                    break;
                case engine::DotInk::Bad:
                    color = bad;
                    break;
                case engine::DotInk::Main:
                    break;
                }
                float const radius = dot.size / 2.0F;
                float const centerY = originY + static_cast<float>(y) * kRowPitch;
                context->FillEllipse(D2D1::Ellipse(D2D1::Point2F(centerX, centerY), radius, radius), theme.brush(withAlpha(color, dot.alpha)));
            }
        }
    }

    engine::TransferId id_;
    engine::TransferState state_ = engine::TransferState::None;
    int columns_ = 0;
    std::vector<engine::PieceDot> dots_;
    wchar_t const* art_ = nullptr;
    ComPtr<ID2D1Bitmap1> bitmap_;
    ComPtr<ID2D1BitmapBrush> brush_;
    bool dark_ = false;
    UINT pixelWidth_ = 0;
    UINT pixelHeight_ = 0;
};

/// A value the Mac lets you select. A right click stands in for that here: it copies.
class FactValue : public View
{
public:
    explicit FactValue(bool monospace)
    {
        interactive_ = true;
        takesFocus_ = false;
        if (monospace)
        {
            Window::get().renderer().dwrite()->CreateTextFormat(
                L"Consolas",
                nullptr,
                DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                kHashSize,
                L"en-us",
                &mono_);
        }
    }

    void setText(std::wstring_view text)
    {
        if (text != text_)
        {
            text_ = text;
            invalidate();
        }
    }

    std::wstring const& text() const { return text_; }

    bool onMouse(MouseEvent const& event) override
    {
        if (event.kind != MouseKind::RightClick || text_.empty())
        {
            return false;
        }
        MenuItem copy;
        copy.title = L"Copy";
        copy.action = [text = text_] { copyText(text); };
        auto const origin = absoluteOrigin();
        PopupMenu::show({ std::move(copy) }, D2D1::Point2F(origin.x + event.point.x, origin.y + event.point.y));
        return true;
    }

    void draw(ID2D1DeviceContext* context) override
    {
        Theme& theme = Theme::get();
        if (mono_ == nullptr)
        {
            theme.text(context, text_, kBody, bounds(), theme.colors().bright, DWRITE_TEXT_ALIGNMENT_LEADING, Trim::Middle, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            return;
        }
        mono_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        context->DrawText(text_.data(), static_cast<UINT32>(text_.size()), mono_.Get(), bounds(), theme.brush(theme.colors().bright), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

private:
    std::wstring text_;
    ComPtr<IDWriteTextFormat> mono_;
};

class OverviewTab : public TabView
{
public:
    explicit OverviewTab(engine::TransferId id)
        : id_(id)
    {
        scroll_ = add<ScrollView>();
        View* content = scroll_->content();

        field_ = content->add<DropletField>(id);
        for (wchar_t const* title : { L"Have", L"Seeds", L"Peers", L"Down", L"Up" })
        {
            stats_.push_back(content->add<Stat>(title));
        }
        for (wchar_t const* title : { L"Saved to", L"Pieces", L"Hash" })
        {
            titles_.push_back(content->add<Label>(title, kBody, ColorRole::Foreground));
        }
        location_ = content->add<FactValue>(false);
        pieces_ = content->add<FactValue>(false);
        hash_ = content->add<FactValue>(true);
    }

    void layout() override
    {
        scroll_->setFrame(bounds());

        float const contentWidth = width() - kMargin * 2.0F;
        field_->setFrame(D2D1::RectF(kMargin, kFieldTop, kMargin + contentWidth, kFieldTop + kFieldHeight));

        float const statsTop = kFieldTop + kFieldHeight + kStatsGap;
        layoutStats(stats_, kMargin, statsTop, contentWidth);

        float top = statsTop + Stat::kHeight + kFactsGap;
        float const valueLeft = kMargin + kFactLabelWidth + kFactGap;
        std::array<FactValue*, 3> const values{ location_, pieces_, hash_ };
        for (size_t i = 0; i < values.size(); ++i)
        {
            titles_[i]->setFrame(D2D1::RectF(kMargin, top, kMargin + kFactLabelWidth, top + kFactHeight));
            values[i]->setFrame(D2D1::RectF(valueLeft, top, kMargin + contentWidth, top + kFactHeight));
            top += kFactPitch;
        }
        scroll_->setContentHeight(top + kBottomPadding);
    }

    void refresh(engine::TransferInfo const& info) override
    {
        field_->update(info);

        TextBuffer buffer;
        stats_[0]->setValue(fileSizeText(info.haveBytes, buffer));
        stats_[1]->setValue(info.seeds >= 0 ? std::to_wstring(info.seeds) : L"?");
        stats_[2]->setValue(std::to_wstring(info.peers >= 0 ? info.peers : info.peersConnected));
        stats_[3]->setValue(speedText(info.downBytesPerSecond, buffer));
        stats_[4]->setValue(speedText(info.upBytesPerSecond, buffer));

        auto const details = App::get().engine().details(id_);
        location_->setText(details.dataPath.empty() ? details.currentFolder : details.dataPath);
        std::wstring pieces = std::to_wstring(details.pieceCount);
        pieces += L" × ";
        pieces += fileSizeText(details.pieceSize, buffer);
        pieces_->setText(pieces);
        hash_->setText(info.hash);
    }

private:
    engine::TransferId id_;
    ScrollView* scroll_ = nullptr;
    DropletField* field_ = nullptr;
    std::vector<Stat*> stats_;
    std::vector<Label*> titles_;
    FactValue* location_ = nullptr;
    FactValue* pieces_ = nullptr;
    FactValue* hash_ = nullptr;
};

} // namespace

std::unique_ptr<TabView> createOverviewTab(engine::TransferId id)
{
    return std::make_unique<OverviewTab>(id);
}

} // namespace app::details
