#include <windows.h>

#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>

#include "app/app.h"
#include "app/shell_integration.h"
#include "app/shell_internal.h"
#include "ui/theme.h"
#include "ui/window.h"

namespace app::shell
{

namespace
{

constexpr UINT kTrayMessage = WM_APP + 78;
constexpr UINT kTrayIconId = 1;
constexpr int kPreferredAppModeOrdinal = 135;
constexpr int kFlushMenuThemesOrdinal = 136;
constexpr int kForceDarkMenus = 2;
constexpr int kForceLightMenus = 3;

enum MenuCommand : UINT
{
    kMenuNone,
    kMenuPauseAll,
    kMenuResumeAll,
    kMenuQuit,
};

HWND trayOwner = nullptr;
HICON trayIcon = nullptr;
UINT taskbarCreated = 0;

template<size_t Size>
void copyTruncated(wchar_t (&out)[Size], std::wstring_view text)
{
    size_t const count = std::min(text.size(), Size - 1);
    std::copy_n(text.data(), count, out);
    out[count] = L'\0';
}

[[nodiscard]] NOTIFYICONDATAW iconData()
{
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = trayOwner;
    data.uID = kTrayIconId;
    return data;
}

void showIcon()
{
    NOTIFYICONDATAW data = iconData();
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data.uCallbackMessage = kTrayMessage;
    data.hIcon = trayIcon;
    wcscpy_s(data.szTip, L"Nagare");
    Shell_NotifyIconW(NIM_ADD, &data);
    data.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &data);
}

/// Win32 menus only go dark through these two unnamed exports. They have kept their ordinals since
/// Windows 10 1903, and on 1809 the first takes a bool, so these values do no harm there.
void matchMenusToTheme()
{
    HMODULE const uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (uxtheme == nullptr)
    {
        return;
    }
    auto const setMode = reinterpret_cast<int(WINAPI*)(int)>(reinterpret_cast<void*>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(kPreferredAppModeOrdinal))));
    auto const flush = reinterpret_cast<void(WINAPI*)()>(reinterpret_cast<void*>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(kFlushMenuThemesOrdinal))));
    if (setMode != nullptr && flush != nullptr)
    {
        setMode(ui::Theme::get().dark() ? kForceDarkMenus : kForceLightMenus);
        flush();
    }
    FreeLibrary(uxtheme);
}

void setAllPaused(bool paused)
{
    std::vector<engine::TransferId> ids;
    for (auto const& transfer : App::get().transfers())
    {
        ids.push_back(transfer.id);
    }
    if (paused)
    {
        App::get().engine().pause(ids);
    }
    else
    {
        App::get().engine().resume(ids);
    }
    App::get().refreshNow();
}

void showMenu(int x, int y)
{
    HMENU const menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kMenuPauseAll, L"Pause All");
    AppendMenuW(menu, MF_STRING, kMenuResumeAll, L"Resume All");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuQuit, L"Quit");
    matchMenusToTheme();

    // Without being the foreground window, a click elsewhere would leave the menu stuck open.
    SetForegroundWindow(trayOwner);
    UINT const flags = TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY | (GetSystemMetrics(SM_MENUDROPALIGNMENT) != 0 ? TPM_RIGHTALIGN : TPM_LEFTALIGN);
    auto const command = static_cast<UINT>(TrackPopupMenuEx(menu, flags, x, y, trayOwner, nullptr));
    PostMessageW(trayOwner, WM_NULL, 0, 0);
    DestroyMenu(menu);

    switch (command)
    {
    case kMenuPauseAll:
        setAllPaused(true);
        break;
    case kMenuResumeAll:
        setAllPaused(false);
        break;
    case kMenuQuit:
        requestQuit();
        break;
    default:
        break;
    }
}

} // namespace

void addTrayIcon(HWND window)
{
    trayOwner = window;
    taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    UINT const dpi = GetDpiForWindow(window);
    trayIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), L"IDI_APP", IMAGE_ICON, GetSystemMetricsForDpi(SM_CXSMICON, dpi), GetSystemMetricsForDpi(SM_CYSMICON, dpi), LR_DEFAULTCOLOR));
    showIcon();
}

void removeTrayIcon()
{
    if (trayOwner == nullptr)
    {
        return;
    }
    NOTIFYICONDATAW data = iconData();
    Shell_NotifyIconW(NIM_DELETE, &data);
    DestroyIcon(trayIcon);
    trayIcon = nullptr;
    trayOwner = nullptr;
}

bool handleTrayMessage(UINT message, WPARAM wparam, LPARAM lparam)
{
    if (trayOwner == nullptr)
    {
        return false;
    }
    if (message == taskbarCreated)
    {
        // Explorer restarted, and with it the notification area, empty.
        showIcon();
        return true;
    }
    if (message != kTrayMessage)
    {
        return false;
    }
    switch (LOWORD(lparam))
    {
    case NIN_SELECT:
    case NIN_KEYSELECT:
    case NIN_BALLOONUSERCLICK:
        ui::Window::get().bringForward();
        break;
    case WM_CONTEXTMENU:
        showMenu(GET_X_LPARAM(wparam), GET_Y_LPARAM(wparam));
        break;
    default:
        break;
    }
    return true;
}

bool userWantsQuiet()
{
    QUERY_USER_NOTIFICATION_STATE state = QUNS_ACCEPTS_NOTIFICATIONS;
    SHQueryUserNotificationState(&state);
    return state == QUNS_BUSY || state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_PRESENTATION_MODE || state == QUNS_QUIET_TIME;
}

void notify(std::wstring_view title, std::wstring_view body, bool sound)
{
    if (trayOwner == nullptr)
    {
        return;
    }
    NOTIFYICONDATAW data = iconData();
    data.uFlags = NIF_INFO;
    data.dwInfoFlags = NIIF_USER | NIIF_RESPECT_QUIET_TIME | (sound && !userWantsQuiet() ? 0U : NIIF_NOSOUND);
    data.hBalloonIcon = trayIcon;
    copyTruncated(data.szInfoTitle, title);
    copyTruncated(data.szInfo, body);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

} // namespace app::shell
