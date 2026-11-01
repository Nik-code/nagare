#include "app/link_panel.h"

#include <cwchar>

#include "app/add_panel.h"
#include "app/app.h"
#include "app/details_common.h"

namespace app
{

namespace
{

using namespace ui;
using details::pasteText;
using details::refuse;
using details::trimmed;

// The numbers in LinkPanelController.mm.
constexpr float kPanelWidth = 560.0F;
constexpr float kPanelHeight = 150.0F;
constexpr float kFieldLeft = 26.0F;
constexpr float kFieldTop = 32.0F;
constexpr float kFieldHeight = 28.0F;
constexpr float kIconSize = 22.0F;
constexpr float kIconGap = 12.0F;
constexpr float kButtonInset = 22.0F;
constexpr float kButtonGap = 10.0F;
constexpr float kCancelWidth = 70.0F;
constexpr float kGetWidth = 76.0F;
constexpr float kStatusHeight = 16.0F;
constexpr float kStatusGap = 16.0F;

constexpr TextStyle kFieldText{ 20.0F, DWRITE_FONT_WEIGHT_NORMAL, false };
constexpr TextStyle kStatusText{ 13.0F, DWRITE_FONT_WEIGHT_NORMAL, false };

// NSGlassEffectViewStyleClear with its tint: dot rows stay readable through it, just.
constexpr float kClearFrost = 0.45F;
constexpr float kClearTint = 1.1F;

bool startsWith(std::wstring_view text, std::wstring_view prefix)
{
    return text.size() >= prefix.size() && _wcsnicmp(text.data(), prefix.data(), prefix.size()) == 0;
}

class LeadingIcon final : public View
{
public:
    explicit LeadingIcon(std::wstring name)
        : name_(std::move(name))
    {
    }

    void draw(ID2D1DeviceContext* context) override { Theme::get().icon(context, name_, bounds(), Theme::get().colors().bright); }

private:
    std::wstring name_;
};

} // namespace

bool looksLikeLink(std::wstring_view text)
{
    if (startsWith(text, L"magnet:"))
    {
        return true;
    }
    if (!startsWith(text, L"https://"))
    {
        return false;
    }
    std::wstring lower(text);
    CharLowerBuffW(lower.data(), static_cast<DWORD>(lower.size()));
    return lower.find(L".torrent") != std::wstring::npos;
}

void openLink(std::wstring const& link, std::function<void(std::wstring const& error)> done)
{
    auto& engine = App::get().engine();
    if (startsWith(link, L"magnet:"))
    {
        engine::AddPreview preview = engine.parseMagnet(link);
        if (!preview.valid)
        {
            done(preview.error.empty() ? L"That isn't a valid magnet link." : preview.error);
            return;
        }
        done({});
        App::get().openPreview(std::move(preview));
        return;
    }

    // Only https is ever fetched, so that is what a bare address gets instead of the Mac's http.
    std::wstring const url = link.find(L"://") == std::wstring::npos ? L"https://" + link : link;
    engine.fetchTorrent(
        url,
        [done = std::move(done)](std::string bytes, std::wstring const& error)
        {
            if (bytes.empty())
            {
                done(error.empty() ? L"That link couldn't be fetched." : error);
                return;
            }
            engine::AddPreview preview = App::get().engine().parseTorrentData(std::move(bytes));
            if (!preview.valid)
            {
                done(preview.error.empty() ? L"That isn't a valid torrent file." : preview.error);
                return;
            }
            done({});
            App::get().openPreview(std::move(preview));
        });
}

LinkPanel::LinkPanel()
    : Panel(kPanelWidth, kPanelHeight)
{
    add<LeadingIcon>(L"Link")->setFrame(D2D1::RectF(kFieldLeft, kFieldTop + (kFieldHeight - kIconSize) / 2.0F, kFieldLeft + kIconSize, kFieldTop + (kFieldHeight + kIconSize) / 2.0F));

    field_ = add<TextField>();
    field_->setFieldStyle(FieldStyle::Plain);
    field_->setTextStyle(kFieldText);
    field_->setPlaceholder(L"Paste a magnet or torrent link");
    field_->onChange = [this](std::wstring const&) { updateButton(); };
    field_->onSubmit = [this] { get(); };

    status_ = add<Label>(L"", kStatusText, ColorRole::Foreground);
    cancel_ = add<Button>(L"Cancel", ButtonStyle::Glass, [this] { close(); });
    cancel_->setCapsule(true);
    get_ = add<Button>(L"Get", ButtonStyle::Accent, [this] { get(); });

    // A copied link is almost always why this was opened.
    std::wstring const clipboard = trimmed(pasteText());
    if (looksLikeLink(clipboard))
    {
        field_->setText(clipboard);
    }
    updateButton();
}

void LinkPanel::layoutContent()
{
    float const left = kFieldLeft + kIconSize + kIconGap;
    field_->setFrame(D2D1::RectF(left, kFieldTop, width() - kFieldLeft, kFieldTop + kFieldHeight));

    float const bottom = height() - kButtonInset;
    float const top = bottom - Button::kHeight;
    float const getLeft = width() - kButtonInset - kGetWidth;
    float const cancelLeft = getLeft - kButtonGap - kCancelWidth;
    get_->setFrame(D2D1::RectF(getLeft, top, getLeft + kGetWidth, bottom));
    cancel_->setFrame(D2D1::RectF(cancelLeft, top, cancelLeft + kCancelWidth, bottom));

    float const statusTop = top + (Button::kHeight - kStatusHeight) / 2.0F;
    status_->setFrame(D2D1::RectF(kFieldLeft, statusTop, cancelLeft - kStatusGap, statusTop + kStatusHeight));
}

void LinkPanel::adjustGlass(GlassShape& shape) const
{
    Panel::adjustGlass(shape);
    // The Mac uses its clear glass here, and only here.
    shape.clarity = 1.0F - (1.0F - shape.clarity) * kClearFrost;
    D2D1_COLOR_F const tint = Theme::get().colors().surface;
    shape.tint = D2D1::ColorF(tint.r, tint.g, tint.b, shape.tint.a * kClearTint);
}

void LinkPanel::updateButton()
{
    get_->setEnabled(!working_ && !trimmed(field_->text()).empty());
}

void LinkPanel::get()
{
    std::wstring const link = trimmed(field_->text());
    if (link.empty() || working_ || closing())
    {
        return;
    }
    working_ = true;
    updateButton();
    status_->setRole(ColorRole::Foreground);
    status_->setText(L"Fetching…");

    openLink(
        link,
        [this, alive = std::weak_ptr<bool>(alive_)](std::wstring const& error)
        {
            if (alive.expired())
            {
                return;
            }
            working_ = false;
            if (error.empty())
            {
                close();
                return;
            }
            // The Mac raises an alert from the controller. Here the link is still in the field.
            updateButton();
            status_->setRole(ColorRole::Danger);
            status_->setText(error);
            refuse();
        });
}

void openLinkPanel()
{
    if (addPanelShowing())
    {
        return;
    }
    Window& window = Window::get();
    auto* panel = Panel::open<LinkPanel>();
    window.focus(panel->field());
    panel->field()->selectAll();
}

} // namespace app
