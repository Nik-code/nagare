#include "app/details_common.h"

#include <shlobj.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

namespace app::details
{

namespace
{

constexpr double kThousand = 1000.0;
constexpr float kStatGap = 8.0F;
constexpr float kStatTitleHeight = 16.0F;
constexpr float kStatValueTop = 18.0F;
constexpr int kMaxLongDecimals = 2;

constexpr std::array<wchar_t const*, 5> kSizeUnits{ L"KB", L"MB", L"GB", L"TB", L"PB" };
constexpr std::array<int, 5> kSizeDecimals{ 0, 1, 2, 2, 2 };

std::wstring_view viewOf(TextBuffer const& buffer, int length)
{
    return { buffer.data(), length > 0 ? static_cast<size_t>(length) : size_t{ 0 } };
}

/// Drops a fraction's trailing zeros, in place, as the Mac's formatters do.
int stripZeros(TextBuffer& buffer, int length)
{
    if (std::wstring_view(buffer.data(), static_cast<size_t>(length)).find(L'.') == std::wstring_view::npos)
    {
        return length;
    }
    while (length > 0 && buffer[static_cast<size_t>(length) - 1] == L'0')
    {
        --length;
    }
    if (length > 0 && buffer[static_cast<size_t>(length) - 1] == L'.')
    {
        --length;
    }
    buffer[static_cast<size_t>(length)] = L'\0';
    return length;
}

double roundedTo(double value, int decimals)
{
    double const scale = std::pow(10.0, decimals);
    return std::round(value * scale) / scale;
}

bool sameDay(SYSTEMTIME const& a, SYSTEMTIME const& b)
{
    return a.wYear == b.wYear && a.wMonth == b.wMonth && a.wDay == b.wDay;
}

SYSTEMTIME localTime(int64_t unixSeconds)
{
    constexpr int64_t kUnixEpochIn100ns = 116444736000000000;
    constexpr int64_t kTicksPerSecond = 10000000;

    ULARGE_INTEGER ticks{};
    ticks.QuadPart = static_cast<ULONGLONG>(unixSeconds * kTicksPerSecond + kUnixEpochIn100ns);
    FILETIME fileTime{};
    fileTime.dwLowDateTime = ticks.LowPart;
    fileTime.dwHighDateTime = ticks.HighPart;

    SYSTEMTIME utc{};
    FileTimeToSystemTime(&fileTime, &utc);
    SYSTEMTIME local{};
    SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local);
    return local;
}

SYSTEMTIME dayBefore(SYSTEMTIME day)
{
    constexpr ULONGLONG kTicksPerDay = 864000000000ULL;

    FILETIME fileTime{};
    SystemTimeToFileTime(&day, &fileTime);
    ULARGE_INTEGER ticks{};
    ticks.LowPart = fileTime.dwLowDateTime;
    ticks.HighPart = fileTime.dwHighDateTime;
    ticks.QuadPart -= kTicksPerDay;
    fileTime.dwLowDateTime = ticks.LowPart;
    fileTime.dwHighDateTime = ticks.HighPart;

    SYSTEMTIME result{};
    FileTimeToSystemTime(&fileTime, &result);
    return result;
}

} // namespace

std::wstring_view fileSizeText(uint64_t bytes, TextBuffer& out)
{
    if (bytes == 0)
    {
        return L"Zero KB";
    }
    if (bytes == 1)
    {
        return L"1 byte";
    }
    if (bytes < 1000)
    {
        return viewOf(out, swprintf_s(out.data(), out.size(), L"%llu bytes", static_cast<unsigned long long>(bytes)));
    }

    double value = static_cast<double>(bytes) / kThousand;
    size_t unit = 0;
    while (unit + 1 < kSizeUnits.size() && roundedTo(value, kSizeDecimals[unit]) >= kThousand)
    {
        value /= kThousand;
        ++unit;
    }
    int const length = swprintf_s(out.data(), out.size(), L"%.*f", kSizeDecimals[unit], value);
    int const kept = stripZeros(out, length);
    return viewOf(out, kept + swprintf_s(out.data() + kept, out.size() - static_cast<size_t>(kept), L" %s", kSizeUnits[unit]));
}

std::wstring_view speedText(double bytesPerSecond, TextBuffer& out)
{
    double speed = bytesPerSecond / kThousand;
    if (speed < 999.95)
    {
        return viewOf(out, swprintf_s(out.data(), out.size(), L"%.1f KB/s", speed));
    }
    speed /= kThousand;
    if (speed < 99.995)
    {
        return viewOf(out, swprintf_s(out.data(), out.size(), L"%.2f MB/s", speed));
    }
    if (speed < 999.95)
    {
        return viewOf(out, swprintf_s(out.data(), out.size(), L"%.1f MB/s", speed));
    }
    speed /= kThousand;
    return viewOf(out, swprintf_s(out.data(), out.size(), speed < 99.995 ? L"%.2f GB/s" : L"%.1f GB/s", speed));
}

std::wstring_view percentText(double progress, int decimals, TextBuffer& out)
{
    if (progress >= 1.0)
    {
        return L"100%";
    }
    // Stops short of 100, because only a finished file may say so.
    double const ceiling = decimals >= kMaxLongDecimals ? 0.9999 : 0.999;
    int const length = swprintf_s(out.data(), out.size(), L"%.*f", decimals, std::min(progress, ceiling) * 100.0);
    int const kept = stripZeros(out, length);
    out[static_cast<size_t>(kept)] = L'%';
    return viewOf(out, kept + 1);
}

std::wstring_view ratioText(double ratio, TextBuffer& out)
{
    constexpr int kRatioNotAvailable = -1;
    constexpr int kRatioInfinite = -2;

    int const whole = static_cast<int>(ratio);
    if (whole == kRatioNotAvailable)
    {
        return L"N/A";
    }
    if (whole == kRatioInfinite)
    {
        return L"∞";
    }
    int const decimals = ratio < 10.0 ? 2 : ratio < 100.0 ? 1 : 0;
    double const scale = std::pow(10.0, decimals);
    return viewOf(out, swprintf_s(out.data(), out.size(), L"%.*f", decimals, std::floor(ratio * scale) / scale));
}

std::wstring dateText(int64_t unixSeconds)
{
    SYSTEMTIME const when = localTime(unixSeconds);
    SYSTEMTIME today{};
    GetLocalTime(&today);

    wchar_t day[96]{};
    if (sameDay(when, today))
    {
        wcscpy_s(day, L"Today");
    }
    else if (sameDay(when, dayBefore(today)))
    {
        wcscpy_s(day, L"Yesterday");
    }
    else
    {
        GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_LONGDATE, &when, nullptr, day, static_cast<int>(std::size(day)), nullptr);
    }

    wchar_t time[48]{};
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &when, nullptr, time, static_cast<int>(std::size(time)));

    std::wstring text = day;
    text += L" at ";
    text += time;
    return text;
}

std::wstring durationText(int64_t seconds)
{
    seconds = std::max<int64_t>(seconds, 0);
    int64_t const hours = seconds / 3600;
    int64_t const minutes = seconds / 60 % 60;
    int64_t const rest = seconds % 60;

    std::wstring text;
    if (hours > 0)
    {
        text = std::to_wstring(hours) + L"h ";
    }
    if (hours > 0 || minutes > 0)
    {
        text += std::to_wstring(minutes) + L"m ";
    }
    text += std::to_wstring(rest) + L"s";
    return text;
}

std::wstring trimmed(std::wstring_view text)
{
    constexpr std::wstring_view kSpace = L" \t\r\n　";
    size_t const first = text.find_first_not_of(kSpace);
    if (first == std::wstring_view::npos)
    {
        return {};
    }
    return std::wstring(text.substr(first, text.find_last_not_of(kSpace) - first + 1));
}

bool pathExists(std::wstring const& path)
{
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

void revealPaths(std::vector<std::wstring> const& paths)
{
    std::map<std::wstring, std::vector<PIDLIST_ABSOLUTE>> byFolder;
    for (std::wstring const& path : paths)
    {
        if (PIDLIST_ABSOLUTE item = ILCreateFromPathW(path.c_str()); item != nullptr)
        {
            byFolder[path.substr(0, path.find_last_of(L'\\'))].push_back(item);
        }
    }
    for (auto& [folder, items] : byFolder)
    {
        if (PIDLIST_ABSOLUTE parent = ILCreateFromPathW(folder.c_str()); parent != nullptr)
        {
            SHOpenFolderAndSelectItems(parent, static_cast<UINT>(items.size()), static_cast<PCUITEMID_CHILD_ARRAY>(static_cast<void*>(items.data())), 0);
            ILFree(parent);
        }
        for (PIDLIST_ABSOLUTE item : items)
        {
            ILFree(item);
        }
    }
}

void openPath(std::wstring const& path)
{
    ShellExecuteW(ui::Window::get().handle(), L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void copyText(std::wstring_view text)
{
    if (!OpenClipboard(ui::Window::get().handle()))
    {
        return;
    }
    EmptyClipboard();
    size_t const bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL const memory = GlobalAlloc(GMEM_MOVEABLE, bytes); memory != nullptr)
    {
        if (auto* locked = static_cast<wchar_t*>(GlobalLock(memory)); locked != nullptr)
        {
            std::memcpy(locked, text.data(), text.size() * sizeof(wchar_t));
            locked[text.size()] = L'\0';
            GlobalUnlock(memory);
            SetClipboardData(CF_UNICODETEXT, memory);
        }
        else
        {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
}

std::wstring pasteText()
{
    std::wstring text;
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(ui::Window::get().handle()))
    {
        return text;
    }
    if (HANDLE const data = GetClipboardData(CF_UNICODETEXT); data != nullptr)
    {
        if (auto const* locked = static_cast<wchar_t const*>(GlobalLock(data)); locked != nullptr)
        {
            text = locked;
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return text;
}

void refuse()
{
    MessageBeep(MB_OK);
}

Stat::Stat(std::wstring title)
{
    title_ = add<ui::Label>(std::move(title), kSmall, ui::ColorRole::Foreground);
    value_ = add<ui::Label>(L"", kStatValue, ui::ColorRole::Bright);
}

void Stat::setValue(std::wstring_view value)
{
    value_->setText(value);
}

void Stat::layout()
{
    title_->setFrame(D2D1::RectF(0.0F, 0.0F, width(), kStatTitleHeight));
    value_->setFrame(D2D1::RectF(0.0F, kStatValueTop, width(), height()));
}

void layoutStats(std::vector<Stat*> const& views, float left, float top, float width)
{
    float const count = static_cast<float>(views.size());
    float const each = (width - kStatGap * (count - 1.0F)) / count;
    float x = left;
    for (Stat* view : views)
    {
        view->setFrame(D2D1::RectF(x, top, x + each, top + Stat::kHeight));
        x += each + kStatGap;
    }
}

} // namespace app::details
