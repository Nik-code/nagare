#include "app/transfer_row.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "ui/widgets_internal.h"

namespace app
{

namespace
{

using engine::TransferState;
using ui::TextStyle;
using ui::Theme;

// DropletCell.mm, with the 6 points NSOutlineView indents a cell already added in.
constexpr float kCellInset = 30.0F;
constexpr float kDropletGap = 22.0F;
constexpr float kDropletRight = 182.0F;
constexpr float kMinDropletWidth = 60.0F;
constexpr float kButtonSize = 30.0F;
constexpr float kButtonGap = 6.0F;
constexpr float kButtonIcon = 20.0F;

constexpr float kNameTop = 16.0F;
constexpr float kNameHeight = 20.0F;
constexpr float kStateTop = 37.0F;
constexpr float kStateHeight = 18.0F;
constexpr float kDetailTop = 56.0F;
constexpr float kDetailHeight = 17.0F;
constexpr float kSpeedTop = 26.0F;
constexpr float kSpeedHeight = 19.0F;
constexpr float kSwarmTop = 46.0F;
constexpr float kSwarmHeight = 17.0F;
constexpr float kLoneRateTop = 35.0F;

constexpr TextStyle kSuffixStyle{ 13.0F, DWRITE_FONT_WEIGHT_NORMAL, false };
constexpr TextStyle kDetailStyle{ 12.0F, DWRITE_FONT_WEIGHT_NORMAL, false };
constexpr TextStyle kSpeedStyle{ 14.0F, DWRITE_FONT_WEIGHT_NORMAL, false };
constexpr TextStyle kSwarmStyle{ 12.0F, DWRITE_FONT_WEIGHT_NORMAL, false };

constexpr double kKilobyte = 1000.0;
constexpr int64_t kSecondsPerMinute = 60;
constexpr int64_t kSecondsPerHour = 3600;
constexpr int64_t kSecondsPerDay = 86400;
constexpr int kBigCount = 1000;
constexpr size_t kLineLength = 1024;

template<typename... Args>
void format(std::wstring& out, wchar_t const* pattern, Args... args)
{
    wchar_t buffer[kLineLength];
    int const length = swprintf(buffer, std::size(buffer), pattern, args...);
    out.assign(buffer, length > 0 ? static_cast<size_t>(length) : 0U);
}

std::wstring timeLeft(int64_t seconds)
{
    wchar_t buffer[32];
    if (seconds >= kSecondsPerDay)
    {
        int64_t const days = seconds / kSecondsPerDay;
        swprintf(buffer, std::size(buffer), days == 1 ? L"%lld day left" : L"%lld days left", days);
    }
    else if (seconds >= kSecondsPerHour)
    {
        swprintf(buffer, std::size(buffer), L"%lld hr left", seconds / kSecondsPerHour);
    }
    else if (seconds >= kSecondsPerMinute)
    {
        swprintf(buffer, std::size(buffer), L"%lld min left", seconds / kSecondsPerMinute);
    }
    else
    {
        swprintf(buffer, std::size(buffer), L"%lld sec left", seconds);
    }
    return buffer;
}

std::wstring shortCount(int count)
{
    wchar_t buffer[24];
    if (count < kBigCount)
    {
        swprintf(buffer, std::size(buffer), L"%d", count);
    }
    else
    {
        swprintf(buffer, std::size(buffer), L"%.1fk", count / static_cast<double>(kBigCount));
    }
    return buffer;
}

void swarmText(engine::TransferInfo const& info, std::wstring& out)
{
    if (info.isMagnet || info.state == TransferState::Problem)
    {
        out.clear();
        return;
    }
    if (info.seeds >= 0 || info.peers >= 0)
    {
        format(out, L"%ls seeds  ·  %ls peers", shortCount(std::max(info.seeds, 0)).c_str(), shortCount(std::max(info.peers, 0)).c_str());
        return;
    }
    // No tracker has reported, so all we can say is who we're talking to.
    if (info.peersConnected > 0)
    {
        format(out, L"%d connected", info.peersConnected);
        return;
    }
    out.clear();
}

std::wstring ratioText(float ratio)
{
    constexpr int kRatioNotAvailable = -1;
    constexpr int kRatioInfinite = -2;
    if (static_cast<int>(ratio) == kRatioNotAvailable)
    {
        return L"N/A";
    }
    if (static_cast<int>(ratio) == kRatioInfinite)
    {
        return L"∞";
    }

    // Truncated rather than rounded, or 1.999 would claim a goal it hasn't reached.
    wchar_t buffer[24];
    if (ratio < 10.0F)
    {
        swprintf(buffer, std::size(buffer), L"%.2f", std::floor(static_cast<double>(ratio) * 100.0) / 100.0);
    }
    else if (ratio < 100.0F)
    {
        swprintf(buffer, std::size(buffer), L"%.1f", std::floor(static_cast<double>(ratio) * 10.0) / 10.0);
    }
    else
    {
        swprintf(buffer, std::size(buffer), L"%.0f", std::floor(static_cast<double>(ratio)));
    }
    return buffer;
}

} // namespace

std::wstring sizeText(uint64_t bytes)
{
    if (bytes == 0)
    {
        return L"Zero KB";
    }
    if (bytes == 1)
    {
        return L"1 byte";
    }
    wchar_t buffer[32];
    auto const value = static_cast<double>(bytes);
    if (value < kKilobyte)
    {
        swprintf(buffer, std::size(buffer), L"%llu bytes", static_cast<unsigned long long>(bytes));
    }
    else if (value < kKilobyte * kKilobyte)
    {
        swprintf(buffer, std::size(buffer), L"%.0f KB", value / kKilobyte);
    }
    else if (value < kKilobyte * kKilobyte * kKilobyte)
    {
        swprintf(buffer, std::size(buffer), L"%.1f MB", value / (kKilobyte * kKilobyte));
    }
    else if (value < kKilobyte * kKilobyte * kKilobyte * kKilobyte)
    {
        swprintf(buffer, std::size(buffer), L"%.2f GB", value / (kKilobyte * kKilobyte * kKilobyte));
    }
    else
    {
        swprintf(buffer, std::size(buffer), L"%.2f TB", value / (kKilobyte * kKilobyte * kKilobyte * kKilobyte));
    }
    return buffer;
}

std::wstring speedText(double bytesPerSecond)
{
    double speed = bytesPerSecond / kKilobyte;
    wchar_t buffer[32];
    if (speed < 999.95)
    {
        swprintf(buffer, std::size(buffer), L"%.1f KB/s", speed);
        return buffer;
    }
    speed /= kKilobyte;
    if (speed < 99.995)
    {
        swprintf(buffer, std::size(buffer), L"%.2f MB/s", speed);
        return buffer;
    }
    if (speed < 999.95)
    {
        swprintf(buffer, std::size(buffer), L"%.1f MB/s", speed);
        return buffer;
    }
    speed /= kKilobyte;
    swprintf(buffer, std::size(buffer), speed < 99.995 ? L"%.2f GB/s" : L"%.1f GB/s", speed);
    return buffer;
}

std::wstring countText(int count)
{
    std::wstring digits = std::to_wstring(count);
    for (int at = static_cast<int>(digits.size()) - 3; at > 0; at -= 3)
    {
        digits.insert(static_cast<size_t>(at), 1, L',');
    }
    return digits;
}

void fillRowText(engine::TransferInfo const& info, int givingBackTo, RowText& out)
{
    using ui::ColorRole;

    out.name = info.name;
    out.suffix.clear();
    out.speed.clear();
    out.stateRole = ColorRole::Foreground;

    auto const have = sizeText(info.haveBytes);
    auto const total = sizeText(info.totalBytes);
    auto const setPercent = [&out, &info] { format(out.suffix, L"  %.0f%%", static_cast<double>(info.percentDone) * 100.0); };

    switch (info.state)
    {
    case TransferState::Problem:
        out.state = L"Problem";
        out.stateRole = ColorRole::Danger;
        out.detail = info.error;
        break;
    case TransferState::Verifying:
        out.state = L"Verifying";
        out.stateRole = ColorRole::Accent;
        if (info.verifyProgress > 0.0F)
        {
            format(out.suffix, L"  %.0f%%", static_cast<double>(info.verifyProgress) * 100.0);
        }
        out.detail = total;
        break;
    case TransferState::Fetching:
        out.state = L"Fetching details";
        out.stateRole = ColorRole::Accent;
        out.detail = L"Waiting for the file list";
        break;
    case TransferState::Seeding:
        out.state = L"Seeding";
        out.stateRole = ColorRole::Seed;
        format(out.suffix, L"  Ratio %ls", ratioText(info.ratio).c_str());
        if (info.ratioStop > 0.0)
        {
            wchar_t goal[24];
            swprintf(goal, std::size(goal), L" of %.1f", info.ratioStop);
            out.suffix += goal;
        }
        if (givingBackTo > 0)
        {
            format(out.detail, L"%ls  ·  giving back to %d peers", total.c_str(), givingBackTo);
        }
        else
        {
            out.detail = total;
        }
        if (info.upBytesPerSecond > 0.0)
        {
            out.speed = speedText(info.upBytesPerSecond);
        }
        break;
    case TransferState::Download:
        out.state = L"Downloading";
        out.stateRole = ColorRole::Accent;
        setPercent();
        if (info.downBytesPerSecond > 0.0 && info.showEta && info.etaSeconds > 0 && info.etaSeconds != engine::kEtaUnknown)
        {
            format(out.detail, L"%ls of %ls  ·  %ls", have.c_str(), total.c_str(), timeLeft(info.etaSeconds).c_str());
        }
        else
        {
            format(out.detail, L"%ls of %ls", have.c_str(), total.c_str());
        }
        if (info.downBytesPerSecond > 0.0)
        {
            out.speed = speedText(info.downBytesPerSecond);
        }
        break;
    case TransferState::Finished:
        out.state = L"Finished";
        out.detail = total;
        break;
    default:
        if (info.isMagnet)
        {
            out.state = L"Paused";
            out.detail = L"Waiting for the file list";
            break;
        }
        out.state = info.isWaiting ? L"Queued" : L"Paused";
        setPercent();
        format(out.detail, L"%ls of %ls", have.c_str(), total.c_str());
        break;
    }

    swarmText(info, out.swarm);
    out.running = info.isWaiting || info.state == TransferState::Download || info.state == TransferState::Fetching || info.state == TransferState::Seeding ||
        info.state == TransferState::Verifying;
}

std::wstring middleTrimmed(std::wstring_view text, TextStyle style, float width)
{
    Theme& theme = Theme::get();
    if (theme.measure(text, style).width <= width)
    {
        return std::wstring(text);
    }

    auto const candidate = [text](size_t keep)
    {
        std::wstring result(text.substr(0, keep - keep / 2));
        result += L'…';
        result += text.substr(text.size() - keep / 2);
        return result;
    };

    size_t low = 0;
    size_t high = text.size();
    while (low + 1 < high)
    {
        size_t const keep = (low + high) / 2;
        if (theme.measure(candidate(keep), style).width <= width)
        {
            low = keep;
        }
        else
        {
            high = keep;
        }
    }
    return candidate(low);
}

D2D1_RECT_F RowGeometry::droplet(D2D1_RECT_F row)
{
    float const left = kCellInset + kNameWidth + kDropletGap;
    float const right = std::max(left + kMinDropletWidth, row.right - kDropletRight);
    float const top = row.top + (row.bottom - row.top - DropletPainter::kHeight) / 2.0F;
    return D2D1::RectF(left, top, right, top + DropletPainter::kHeight);
}

D2D1_RECT_F RowGeometry::button(D2D1_RECT_F row, RowButton button)
{
    float const middle = (row.top + row.bottom) / 2.0F;
    float const right = row.right - kCellInset - (button == RowButton::Control ? kButtonSize + kButtonGap : 0.0F);
    return D2D1::RectF(right - kButtonSize, middle - kButtonSize / 2.0F, right, middle + kButtonSize / 2.0F);
}

void paintRow(ID2D1DeviceContext* context, RowSlot& slot, D2D1_RECT_F row, ui::RowState state, RowButton hovered, bool pressed, DropletPainter& painter)
{
    using namespace ui::detail;
    auto const centred = DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
    auto const leading = DWRITE_TEXT_ALIGNMENT_LEADING;
    auto const trailing = DWRITE_TEXT_ALIGNMENT_TRAILING;

    Theme& theme = Theme::get();
    ui::Palette const& colors = theme.colors();
    float const left = kCellInset;
    float const right = left + RowGeometry::kNameWidth;

    D2D1_RECT_F const nameBox = D2D1::RectF(left, row.top + kNameTop, right, row.top + kNameTop + kNameHeight);
    theme.text(context, slot.shownName, RowGeometry::kNameStyle, nameBox, colors.bright, leading, ui::Trim::None, centred);

    D2D1_RECT_F const stateBox = D2D1::RectF(left, row.top + kStateTop, right, row.top + kStateTop + kStateHeight);
    theme.text(context, slot.text.state, RowGeometry::kStateStyle, stateBox, ui::roleColor(slot.text.stateRole), leading, ui::Trim::None, centred);
    if (!slot.text.suffix.empty())
    {
        D2D1_RECT_F const suffixBox = D2D1::RectF(left + slot.stateWidth, stateBox.top, right, stateBox.bottom);
        theme.text(context, slot.text.suffix, kSuffixStyle, suffixBox, colors.foreground, leading, ui::Trim::None, centred);
    }

    D2D1_RECT_F const detailBox = D2D1::RectF(left, row.top + kDetailTop, right, row.top + kDetailTop + kDetailHeight);
    theme.text(context, slot.text.detail, kDetailStyle, detailBox, colors.foreground, leading, ui::Trim::End, centred);

    painter.paint(context, RowGeometry::droplet(row), DropletPainter::artFor(slot.state, slot.text.name), slot.state, slot.shown, slot.columns, slot.batch);

    if (state.hover)
    {
        for (RowButton const button : { RowButton::Control, RowButton::Remove })
        {
            D2D1_RECT_F const rect = RowGeometry::button(row, button);
            bool const lit = hovered == button;
            if (lit)
            {
                fillRounded(context, rect, kButtonSize / 2.0F, ui::withAlpha(colors.bright, pressed ? 0.06F : 0.10F));
            }
            float const margin = (kButtonSize - kButtonIcon) / 2.0F;
            D2D1_RECT_F const icon = inset(rect, margin, margin);
            bool const isControl = button == RowButton::Control;
            wchar_t const* name = !isControl ? L"Close" : slot.text.running ? L"Pause" : L"Play";
            theme.icon(context, name, icon, isControl || lit ? colors.bright : colors.foreground);
        }
        return;
    }

    float const rateLeft = row.right - kDropletRight;
    float const rateRight = row.right - kCellInset;
    bool const both = !slot.text.speed.empty() && !slot.text.swarm.empty();
    if (!slot.text.speed.empty())
    {
        float const top = row.top + (both ? kSpeedTop : kLoneRateTop);
        theme.text(context, slot.text.speed, kSpeedStyle, D2D1::RectF(rateLeft, top, rateRight, top + kSpeedHeight), colors.bright, trailing, ui::Trim::None, centred);
    }
    if (!slot.text.swarm.empty())
    {
        float const top = row.top + (both ? kSwarmTop : kLoneRateTop);
        theme.text(context, slot.text.swarm, kSwarmStyle, D2D1::RectF(rateLeft, top, rateRight, top + kSwarmHeight), colors.foreground, trailing, ui::Trim::None, centred);
    }
}

} // namespace app
