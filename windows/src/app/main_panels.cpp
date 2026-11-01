#include "app/main_panels.h"

#include <cmath>

#include "ui/widgets.h"
#include "ui/widgets_internal.h"

namespace app
{

namespace
{

using ui::Button;
using ui::ButtonStyle;
using ui::Panel;
using ui::TextStyle;
using ui::Theme;
using ui::Window;

constexpr float kWidth = 440.0F;
constexpr float kPadding = 24.0F;
constexpr float kTitleGap = 8.0F;
constexpr float kButtonGap = 8.0F;
constexpr float kButtonsGap = 20.0F;
constexpr float kMinButtonWidth = 84.0F;
constexpr TextStyle kTitleStyle{ 15.0F, DWRITE_FONT_WEIGHT_SEMI_BOLD, false };
constexpr TextStyle kMessageStyle{ 13.0F, DWRITE_FONT_WEIGHT_NORMAL, false };

ComPtr<IDWriteTextLayout> wrapped(std::wstring const& text, TextStyle style, float width)
{
    ComPtr<IDWriteTextLayout> layout;
    Window::get().renderer().dwrite()->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), Theme::get().format(style), width, 4096.0F, &layout);
    if (layout != nullptr)
    {
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }
    return layout;
}

float heightOf(IDWriteTextLayout* layout)
{
    DWRITE_TEXT_METRICS metrics{};
    if (layout != nullptr)
    {
        layout->GetMetrics(&metrics);
    }
    return std::ceil(metrics.height);
}

class ConfirmPanel final : public Panel
{
public:
    explicit ConfirmPanel(ConfirmRequest request)
        : Panel(kWidth, 0.0F)
        , request_(std::move(request))
    {
        float const textWidth = kWidth - kPadding * 2.0F;
        title_ = wrapped(request_.title, kTitleStyle, textWidth);
        message_ = wrapped(request_.message, kMessageStyle, textWidth);
        titleHeight_ = heightOf(title_.Get());
        messageHeight_ = request_.message.empty() ? 0.0F : heightOf(message_.Get());

        confirm_ = add<Button>(request_.confirm, ButtonStyle::Accent, [this] { accept(); });
        if (!request_.cancel.empty())
        {
            cancel_ = add<Button>(request_.cancel, ButtonStyle::Glass, [this] { close(); });
        }
        setPanelSize(kWidth, kPadding * 2.0F + titleHeight_ + kTitleGap + messageHeight_ + kButtonsGap + Button::kHeight);
    }

    void draw(ID2D1DeviceContext* context) override
    {
        Theme& theme = Theme::get();
        context->DrawTextLayout(D2D1::Point2F(kPadding, kPadding), title_.Get(), theme.brush(theme.colors().bright));
        if (message_ != nullptr)
        {
            context->DrawTextLayout(D2D1::Point2F(kPadding, kPadding + titleHeight_ + kTitleGap), message_.Get(), theme.brush(theme.colors().foreground));
        }
    }

    bool onKey(UINT key, bool down) override
    {
        if (key == VK_RETURN)
        {
            if (down)
            {
                accept();
            }
            return true;
        }
        return Panel::onKey(key, down);
    }

protected:
    void layoutContent() override
    {
        float const bottom = height() - kPadding;
        float const top = bottom - Button::kHeight;
        float right = width() - kPadding;
        float const confirmWidth = std::max(kMinButtonWidth, confirm_->preferredWidth());
        confirm_->setFrame(D2D1::RectF(right - confirmWidth, top, right, bottom));
        right -= confirmWidth + kButtonGap;
        if (cancel_ != nullptr)
        {
            float const cancelWidth = std::max(kMinButtonWidth, cancel_->preferredWidth());
            cancel_->setFrame(D2D1::RectF(right - cancelWidth, top, right, bottom));
        }
    }

private:
    void accept()
    {
        auto action = std::move(request_.onConfirm);
        close();
        if (action)
        {
            action();
        }
    }

    ConfirmRequest request_;
    ComPtr<IDWriteTextLayout> title_;
    ComPtr<IDWriteTextLayout> message_;
    float titleHeight_ = 0.0F;
    float messageHeight_ = 0.0F;
    Button* confirm_ = nullptr;
    Button* cancel_ = nullptr;
};

class RenamePanel final : public Panel
{
public:
    RenamePanel(std::wstring const& current, std::function<void(std::wstring const&)> onRename)
        : Panel(kWidth, 0.0F)
        , onRename_(std::move(onRename))
    {
        title_ = wrapped(L"Rename", kTitleStyle, kWidth - kPadding * 2.0F);
        titleHeight_ = heightOf(title_.Get());

        field_ = add<ui::TextField>();
        field_->setText(current);
        field_->selectAll();
        field_->onSubmit = [this] { accept(); };

        confirm_ = add<Button>(L"Rename", ButtonStyle::Accent, [this] { accept(); });
        cancel_ = add<Button>(L"Cancel", ButtonStyle::Glass, [this] { close(); });
        setPanelSize(kWidth, kPadding * 2.0F + titleHeight_ + kTitleGap + ui::TextField::kHeight + kButtonsGap + Button::kHeight);
    }

    void draw(ID2D1DeviceContext* context) override
    {
        Theme& theme = Theme::get();
        context->DrawTextLayout(D2D1::Point2F(kPadding, kPadding), title_.Get(), theme.brush(theme.colors().bright));
    }

    void focusField() { Window::get().focus(field_); }

protected:
    void layoutContent() override
    {
        float const top = kPadding + titleHeight_ + kTitleGap;
        field_->setFrame(D2D1::RectF(kPadding, top, width() - kPadding, top + ui::TextField::kHeight));

        float const bottom = height() - kPadding;
        float right = width() - kPadding;
        float const confirmWidth = std::max(kMinButtonWidth, confirm_->preferredWidth());
        confirm_->setFrame(D2D1::RectF(right - confirmWidth, bottom - Button::kHeight, right, bottom));
        right -= confirmWidth + kButtonGap;
        float const cancelWidth = std::max(kMinButtonWidth, cancel_->preferredWidth());
        cancel_->setFrame(D2D1::RectF(right - cancelWidth, bottom - Button::kHeight, right, bottom));
    }

private:
    void accept()
    {
        std::wstring const name = field_->text();
        auto action = std::move(onRename_);
        close();
        if (action && !name.empty())
        {
            action(name);
        }
    }

    std::function<void(std::wstring const&)> onRename_;
    ComPtr<IDWriteTextLayout> title_;
    float titleHeight_ = 0.0F;
    ui::TextField* field_ = nullptr;
    Button* confirm_ = nullptr;
    Button* cancel_ = nullptr;
};

} // namespace

void openConfirmPanel(ConfirmRequest request)
{
    Panel::open<ConfirmPanel>(std::move(request));
}

void openRenamePanel(std::wstring const& current, std::function<void(std::wstring const&)> onRename)
{
    Panel::open<RenamePanel>(current, std::move(onRename))->focusField();
}

} // namespace app
