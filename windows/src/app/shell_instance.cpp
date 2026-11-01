#include <windows.h>

#include <sddl.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string_view>

#include "app/shell_integration.h"
#include "app/shell_internal.h"
#include "ui/window.h"

namespace app::shell
{

namespace
{

constexpr wchar_t kWindowClass[] = L"NagareWindow";
constexpr wchar_t kInstanceProperty[] = L"NagareInstance";
constexpr std::wstring_view kMagnetScheme = L"magnet:";
constexpr DWORD kPollMs = 50;
constexpr ULONGLONG kClaimWaitMs = 10'000;
constexpr UINT kForwardTimeoutMs = 5'000;
constexpr DWORD kQuitWaitMs = 15'000;

// Released by the system when the process ends, so a crash can't block the next launch.
HANDLE instanceMutex = nullptr;

[[nodiscard]] bool startsWith(std::wstring_view text, std::wstring_view prefix)
{
    return text.size() >= prefix.size() && CompareStringOrdinal(text.data(), static_cast<int>(prefix.size()), prefix.data(), static_cast<int>(prefix.size()), TRUE) == CSTR_EQUAL;
}

[[nodiscard]] bool takesValue(std::wstring_view flag)
{
    constexpr std::array<std::wstring_view, 5> kFlags{ L"--shot", L"--open", L"--config", L"--engine-test", L"--shell-test" };
    for (auto const candidate : kFlags)
    {
        if (flag == candidate)
        {
            return true;
        }
    }
    return false;
}

/// Local\ keeps this to one logon session, and the SID to one user within it.
[[nodiscard]] std::wstring mutexName()
{
    std::wstring name = L"Local\\Nagare";
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    {
        return name;
    }
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    auto const buffer = std::make_unique<std::byte[]>(size);
    wchar_t* sid = nullptr;
    if (GetTokenInformation(token, TokenUser, buffer.get(), size, &size) && ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER const*>(buffer.get())->User.Sid, &sid))
    {
        name += L"-";
        name += sid;
        LocalFree(sid);
    }
    CloseHandle(token);
    return name;
}

/// The class alone won't do, because the demo and the build checks share it.
[[nodiscard]] HWND findInstance()
{
    for (HWND window = FindWindowExW(nullptr, nullptr, kWindowClass, nullptr); window != nullptr; window = FindWindowExW(nullptr, window, kWindowClass, nullptr))
    {
        if (GetPropW(window, kInstanceProperty) != nullptr)
        {
            return window;
        }
    }
    return nullptr;
}

void send(HWND window, ULONG_PTR kind, std::wstring_view text)
{
    COPYDATASTRUCT data{};
    data.dwData = kind;
    data.cbData = static_cast<DWORD>(text.size() * sizeof(wchar_t));
    data.lpData = const_cast<wchar_t*>(text.data());
    DWORD_PTR result = 0;
    SendMessageTimeoutW(window, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data), SMTO_ABORTIFHUNG, kForwardTimeoutMs, &result);
}

void forward(HWND window, std::vector<std::wstring> const& requests)
{
    // Only the process the user just launched may hand over the right to come to the front.
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    AllowSetForegroundWindow(processId);
    if (requests.empty())
    {
        send(window, ui::kOpenRequest, {});
    }
    for (std::wstring const& request : requests)
    {
        send(window, ui::kOpenRequest, request);
    }
}

} // namespace

bool runsSideBySide(int argc, wchar_t** argv)
{
    for (int i = 1; i < argc; ++i)
    {
        std::wstring_view const flag = argv[i];
        // A --config run has its own profile, so it mustn't hand its work to the Nagare in daily use.
        if (flag == L"--config" || flag == L"--demo" || flag == L"--demo-light" || flag == L"--shot" || flag == L"--widgets" || (flag.starts_with(L"--") && flag.ends_with(L"-test")))
        {
            return true;
        }
    }
    return false;
}

std::vector<std::wstring> openRequests(int argc, wchar_t** argv)
{
    std::vector<std::wstring> requests;
    for (int i = 1; i < argc; ++i)
    {
        std::wstring_view const argument = argv[i];
        if (argument.starts_with(L"--"))
        {
            i += takesValue(argument) ? 1 : 0;
        }
        else if (startsWith(argument, kMagnetScheme))
        {
            requests.emplace_back(argument);
        }
        else
        {
            // The running instance has its own working folder, so a relative path means nothing there.
            std::error_code error;
            auto const absolute = std::filesystem::absolute(argument, error);
            requests.push_back(error ? std::wstring(argument) : absolute.wstring());
        }
    }
    return requests;
}

bool claimInstance(std::vector<std::wstring> const& requests)
{
    instanceMutex = CreateMutexW(nullptr, FALSE, mutexName().c_str());
    if (instanceMutex == nullptr)
    {
        return true;
    }

    // A running instance may not have its window yet, or may be saving on its way out. In that last
    // case its window has gone and the mutex comes free, so this launch takes over.
    ULONGLONG const deadline = GetTickCount64() + kClaimWaitMs;
    do
    {
        DWORD const wait = WaitForSingleObject(instanceMutex, kPollMs);
        if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED)
        {
            return true;
        }
        if (HWND const running = findInstance(); running != nullptr)
        {
            forward(running, requests);
            return false;
        }
    } while (GetTickCount64() < deadline);
    return false;
}

void publishInstance(HWND window)
{
    if (instanceMutex != nullptr)
    {
        SetPropW(window, kInstanceProperty, window);
    }
}

void withdrawInstance(HWND window)
{
    RemovePropW(window, kInstanceProperty);
}

void closeRunningInstance()
{
    HWND const running = findInstance();
    if (running == nullptr)
    {
        return;
    }
    DWORD processId = 0;
    GetWindowThreadProcessId(running, &processId);
    HANDLE const process = OpenProcess(SYNCHRONIZE, FALSE, processId);
    send(running, ui::kQuitRequest, {});
    if (process != nullptr)
    {
        WaitForSingleObject(process, kQuitWaitMs);
        CloseHandle(process);
    }
}

} // namespace app::shell
