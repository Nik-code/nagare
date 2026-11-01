#include <algorithm>
#include <array>
#include <cstdlib>
#include <cwchar>
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

constexpr float kStackTop = 10.0F;
constexpr float kBottomPadding = 12.0F;

// InfoOptionsViewController.mm
constexpr float kRowHeight = 52.0F;
constexpr float kControlGap = 8.0F;
constexpr float kNumberWidth = 64.0F;
constexpr float kRatioWidth = 56.0F;
constexpr float kPopUpPadding = 8.0F;

constexpr int kNoLimitIndex = 1;
constexpr int kCustomIndex = 2;

enum Row
{
    kPriorityRow,
    kDownloadRow,
    kUploadRow,
    kGlobalRow,
    kRatioRow,
    kIdleRow,
    kRemoveRow,
    kPeersRow,
    kSequentialRow,
    kRowCount,
};

constexpr std::array<wchar_t const*, kRowCount> kTitles{
    L"Priority",
    L"Limit download",
    L"Limit upload",
    L"Stay within the global limits",
    L"Stop seeding at ratio",
    L"Stop seeding when idle",
    L"Remove from the list when done",
    L"Maximum connections",
    L"Download in order",
};

bool isDigit(wchar_t c)
{
    return c >= L'0' && c <= L'9';
}

int priorityIndex(engine::Priority priority)
{
    switch (priority)
    {
    case engine::Priority::High:
        return 0;
    case engine::Priority::Low:
        return 2;
    case engine::Priority::Normal:
        break;
    }
    return 1;
}

constexpr std::array<engine::Priority, 3> kPriorities{ engine::Priority::High, engine::Priority::Normal, engine::Priority::Low };

int limitIndex(engine::LimitMode mode)
{
    switch (mode)
    {
    case engine::LimitMode::Unlimited:
        return kNoLimitIndex;
    case engine::LimitMode::Single:
        return kCustomIndex;
    case engine::LimitMode::Global:
        break;
    }
    return 0;
}

engine::LimitMode limitMode(int index)
{
    return index == kCustomIndex ? engine::LimitMode::Single : index == kNoLimitIndex ? engine::LimitMode::Unlimited : engine::LimitMode::Global;
}

/// Hairlines over each row. Theme's rowWithTitle puts them on top.
class OptionsContent final : public View
{
public:
    void draw(ID2D1DeviceContext* context) override
    {
        Theme& theme = Theme::get();
        float const pixel = detail::hairline();
        for (int row = 0; row < kRowCount; ++row)
        {
            float const y = kStackTop + static_cast<float>(row) * kRowHeight;
            context->FillRectangle(D2D1::RectF(kMargin, y, width() - kMargin, y + pixel), theme.brush(theme.colors().border));
        }
    }
};

class OptionsTab : public TabView
{
public:
    explicit OptionsTab(engine::TransferId id)
        : id_(id)
    {
        scroll_ = add<ScrollView>();
        content_ = scroll_->setContent<OptionsContent>();

        for (size_t i = 0; i < kTitles.size(); ++i)
        {
            titles_[i] = content_->add<Label>(kTitles[i], kRowTitle, ColorRole::Bright);
        }

        priority_ = content_->add<Dropdown>(std::vector<std::wstring>{ L"High", L"Normal", L"Low" }, 1);
        priority_->onChange = [this](int index) { change([index](engine::TransferOptions& options) { options.priority = kPriorities[static_cast<size_t>(index)]; }); };

        downloadField_ = numberField([this] { commitSpeed(false); });
        downloadUnit_ = hint(L"KB/s");
        downloadSwitch_ = limitSwitch(false);

        uploadField_ = numberField([this] { commitSpeed(true); });
        uploadUnit_ = hint(L"KB/s");
        uploadSwitch_ = limitSwitch(true);

        globalSwitch_ = content_->add<Switch>();
        globalSwitch_->onChange = [this](bool on) { change([on](engine::TransferOptions& options) { options.honorSessionLimits = on; }); };

        std::vector<std::wstring> const limitTitles{ L"Global setting", L"Don't stop", L"Custom" };

        ratioGlobal_ = hint(L"");
        ratioField_ = content_->add<TextField>();
        ratioField_->setAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        ratioField_->setFilter([](wchar_t c) { return isDigit(c) || c == L'.'; });
        wireCommit(ratioField_, [this] { commitRatio(); });
        ratioPopUp_ = content_->add<Dropdown>(limitTitles);
        ratioPopUp_->onChange = [this](int index) { setRatioMode(index); };

        idleGlobal_ = hint(L"");
        idleField_ = numberField([this] { commitIdle(); });
        idleUnit_ = hint(L"minutes");
        idlePopUp_ = content_->add<Dropdown>(limitTitles);
        idlePopUp_->onChange = [this](int index) { setIdleMode(index); };

        removeSwitch_ = content_->add<Switch>();
        removeSwitch_->onChange = [this](bool on) { change([on](engine::TransferOptions& options) { options.removeWhenDone = on; }); };

        peersField_ = numberField([this] { commitPeers(); });
        peersUnit_ = hint(L"peers");

        sequentialSwitch_ = content_->add<Switch>();
        sequentialSwitch_->onChange = [this](bool on) { change([on](engine::TransferOptions& options) { options.sequential = on; }); };
    }

    void layout() override
    {
        scroll_->setFrame(bounds());
        scroll_->setContentHeight(kStackTop + kRowHeight * static_cast<float>(kRowCount) + kBottomPadding);
        positionControls();
    }

    /// Everything here is the user's own, so it only changes when they change it.
    void refresh(engine::TransferInfo const&) override {}

    void shown(engine::TransferInfo const&) override
    {
        auto& engine = App::get().engine();
        auto const options = engine.options(id_);
        auto const settings = engine.settings();

        priority_->setSelected(priorityIndex(options.priority));

        downloadSwitch_->setOn(options.limitDown);
        downloadField_->setEnabled(options.limitDown);
        downloadField_->setText(std::to_wstring(options.downKBps));

        uploadSwitch_->setOn(options.limitUp);
        uploadField_->setEnabled(options.limitUp);
        uploadField_->setText(std::to_wstring(options.upKBps));

        globalSwitch_->setOn(options.honorSessionLimits);

        TextBuffer buffer;
        ratioGlobal_->setText(settings.ratioLimitOn ? ratioText(settings.ratioLimit, buffer) : std::wstring_view(L"disabled"));
        ratioPopUp_->setSelected(limitIndex(options.ratioMode));
        ratioField_->setText(ratioValue(options.ratioLimit));

        idleGlobal_->setText(settings.idleLimitOn ? (settings.idleMinutes == 1 ? L"1 minute" : std::to_wstring(settings.idleMinutes) + L" minutes") : L"disabled");
        idlePopUp_->setSelected(limitIndex(options.idleMode));
        idleField_->setText(std::to_wstring(options.idleMinutes));

        removeSwitch_->setOn(options.removeWhenDone);
        peersField_->setText(std::to_wstring(options.peerLimit));
        sequentialSwitch_->setOn(options.sequential);

        positionControls();
    }

private:
    static std::wstring ratioValue(double ratio)
    {
        TextBuffer buffer;
        int const length = swprintf_s(buffer.data(), buffer.size(), L"%.2f", ratio);
        return std::wstring(buffer.data(), static_cast<size_t>(std::max(length, 0)));
    }

    template<typename Change>
    void change(Change&& apply)
    {
        auto& engine = App::get().engine();
        auto options = engine.options(id_);
        apply(options);
        engine.setOptions(id_, options);
        App::get().refreshNow();
    }

    Label* hint(wchar_t const* text)
    {
        auto* label = content_->add<Label>(text, kSmall, ColorRole::Foreground);
        label->setAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        return label;
    }

    void wireCommit(TextField* field, std::function<void()> commit)
    {
        field->onBlur = commit;
        field->onSubmit = [commit = std::move(commit)]
        {
            commit();
            Window::get().focus(nullptr);
        };
    }

    TextField* numberField(std::function<void()> commit)
    {
        auto* field = content_->add<TextField>();
        field->setAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        field->setFilter(isDigit);
        wireCommit(field, std::move(commit));
        return field;
    }

    Switch* limitSwitch(bool upload)
    {
        auto* toggle = content_->add<Switch>();
        toggle->onChange = [this, upload](bool on)
        {
            change(
                [upload, on](engine::TransferOptions& options)
                {
                    (upload ? options.limitUp : options.limitDown) = on;
                });
            TextField* field = upload ? uploadField_ : downloadField_;
            field->setEnabled(on);
            if (on)
            {
                focusField(field);
            }
        };
        return toggle;
    }

    static void focusField(TextField* field)
    {
        Window::get().focus(field);
        field->selectAll();
    }

    /// Bad input puts back the last value that was good, as the Mac's formatters do.
    void commitSpeed(bool upload)
    {
        TextField* field = upload ? uploadField_ : downloadField_;
        auto const options = App::get().engine().options(id_);
        int const current = upload ? options.upKBps : options.downKBps;
        if (field->text().empty())
        {
            field->setText(std::to_wstring(current));
            return;
        }
        int const value = parseCount(field->text());
        field->setText(std::to_wstring(value));
        if (value != current)
        {
            change([upload, value](engine::TransferOptions& next) { (upload ? next.upKBps : next.downKBps) = value; });
        }
    }

    void commitRatio()
    {
        auto const options = App::get().engine().options(id_);
        wchar_t* end = nullptr;
        double const value = std::wcstod(ratioField_->text().c_str(), &end);
        if (ratioField_->text().empty() || end == nullptr || *end != L'\0' || value < 0.0)
        {
            refuseIfTyped(ratioField_);
            ratioField_->setText(ratioValue(options.ratioLimit));
            return;
        }
        ratioField_->setText(ratioValue(value));
        if (value != options.ratioLimit)
        {
            change([value](engine::TransferOptions& next) { next.ratioLimit = value; });
        }
    }

    void commitIdle()
    {
        auto const options = App::get().engine().options(id_);
        if (idleField_->text().empty())
        {
            idleField_->setText(std::to_wstring(options.idleMinutes));
            return;
        }
        int const value = parseCount(idleField_->text());
        idleField_->setText(std::to_wstring(value));
        if (value != options.idleMinutes)
        {
            change([value](engine::TransferOptions& next) { next.idleMinutes = value; });
        }
    }

    void commitPeers()
    {
        auto const options = App::get().engine().options(id_);
        if (peersField_->text().empty())
        {
            peersField_->setText(std::to_wstring(options.peerLimit));
            return;
        }
        int const value = parseCount(peersField_->text());
        peersField_->setText(std::to_wstring(value));
        if (value != options.peerLimit)
        {
            change([value](engine::TransferOptions& next) { next.peerLimit = value; });
        }
    }

    static int parseCount(std::wstring const& text)
    {
        constexpr long kMaxCount = 1'000'000'000L;
        return static_cast<int>(std::min(std::wcstol(text.c_str(), nullptr, 10), kMaxCount));
    }

    static void refuseIfTyped(TextField const* field)
    {
        if (!field->text().empty())
        {
            refuse();
        }
    }

    void setRatioMode(int index)
    {
        change([index](engine::TransferOptions& options) { options.ratioMode = limitMode(index); });
        positionControls();
        if (index == kCustomIndex)
        {
            focusField(ratioField_);
        }
    }

    void setIdleMode(int index)
    {
        change([index](engine::TransferOptions& options) { options.idleMode = limitMode(index); });
        positionControls();
        if (index == kCustomIndex)
        {
            focusField(idleField_);
        }
    }

    static float rowMiddle(int row)
    {
        return kStackTop + (static_cast<float>(row) + 0.5F) * kRowHeight;
    }

    /// Lays a row's controls out leftwards from `right`, the way its stack view would.
    template<typename T>
    static void place(T* view, float viewWidth, float viewHeight, int row, float& right)
    {
        float const top = rowMiddle(row) - viewHeight / 2.0F;
        view->setFrame(D2D1::RectF(right - viewWidth, top, right, top + viewHeight));
        right -= viewWidth + kControlGap;
    }

    static void placeHint(Label* label, int row, float& right)
    {
        constexpr float kHintHeight = 16.0F;
        place(label, label->preferredWidth(), kHintHeight, row, right);
    }

    void positionControls()
    {
        if (width() <= 0.0F)
        {
            return;
        }
        float const edge = width() - kMargin;
        for (int row = 0; row < kRowCount; ++row)
        {
            float const top = rowMiddle(row) - kRowHeight / 2.0F;
            titles_[static_cast<size_t>(row)]->setFrame(D2D1::RectF(kMargin, top, edge, top + kRowHeight));
        }

        float right = edge;
        place(priority_, priority_->preferredWidth() + kPopUpPadding, Dropdown::kHeight, kPriorityRow, right);

        right = edge;
        place(downloadSwitch_, Switch::kWidth, Switch::kHeight, kDownloadRow, right);
        placeHint(downloadUnit_, kDownloadRow, right);
        place(downloadField_, kNumberWidth, TextField::kHeight, kDownloadRow, right);

        right = edge;
        place(uploadSwitch_, Switch::kWidth, Switch::kHeight, kUploadRow, right);
        placeHint(uploadUnit_, kUploadRow, right);
        place(uploadField_, kNumberWidth, TextField::kHeight, kUploadRow, right);

        right = edge;
        place(globalSwitch_, Switch::kWidth, Switch::kHeight, kGlobalRow, right);

        right = edge;
        place(ratioPopUp_, ratioPopUp_->preferredWidth() + kPopUpPadding, Dropdown::kHeight, kRatioRow, right);
        bool const ratioCustom = ratioPopUp_->selected() == kCustomIndex;
        bool const ratioGlobal = ratioPopUp_->selected() == 0;
        ratioField_->setHidden(!ratioCustom);
        ratioGlobal_->setHidden(!ratioGlobal);
        if (ratioCustom)
        {
            place(ratioField_, kRatioWidth, TextField::kHeight, kRatioRow, right);
        }
        if (ratioGlobal)
        {
            placeHint(ratioGlobal_, kRatioRow, right);
        }

        right = edge;
        place(idlePopUp_, idlePopUp_->preferredWidth() + kPopUpPadding, Dropdown::kHeight, kIdleRow, right);
        bool const idleCustom = idlePopUp_->selected() == kCustomIndex;
        bool const idleGlobal = idlePopUp_->selected() == 0;
        idleField_->setHidden(!idleCustom);
        idleUnit_->setHidden(!idleCustom);
        idleGlobal_->setHidden(!idleGlobal);
        if (idleCustom)
        {
            placeHint(idleUnit_, kIdleRow, right);
            place(idleField_, kRatioWidth, TextField::kHeight, kIdleRow, right);
        }
        if (idleGlobal)
        {
            placeHint(idleGlobal_, kIdleRow, right);
        }

        right = edge;
        place(removeSwitch_, Switch::kWidth, Switch::kHeight, kRemoveRow, right);

        right = edge;
        placeHint(peersUnit_, kPeersRow, right);
        place(peersField_, kNumberWidth, TextField::kHeight, kPeersRow, right);

        right = edge;
        place(sequentialSwitch_, Switch::kWidth, Switch::kHeight, kSequentialRow, right);
    }

    engine::TransferId id_;
    ScrollView* scroll_ = nullptr;
    OptionsContent* content_ = nullptr;
    std::array<Label*, kRowCount> titles_{};

    Dropdown* priority_ = nullptr;
    TextField* downloadField_ = nullptr;
    Label* downloadUnit_ = nullptr;
    Switch* downloadSwitch_ = nullptr;
    TextField* uploadField_ = nullptr;
    Label* uploadUnit_ = nullptr;
    Switch* uploadSwitch_ = nullptr;
    Switch* globalSwitch_ = nullptr;
    Label* ratioGlobal_ = nullptr;
    TextField* ratioField_ = nullptr;
    Dropdown* ratioPopUp_ = nullptr;
    Label* idleGlobal_ = nullptr;
    TextField* idleField_ = nullptr;
    Label* idleUnit_ = nullptr;
    Dropdown* idlePopUp_ = nullptr;
    Switch* removeSwitch_ = nullptr;
    TextField* peersField_ = nullptr;
    Label* peersUnit_ = nullptr;
    Switch* sequentialSwitch_ = nullptr;
};

} // namespace

std::unique_ptr<TabView> createOptionsTab(engine::TransferId id)
{
    return std::make_unique<OptionsTab>(id);
}

} // namespace app::details
