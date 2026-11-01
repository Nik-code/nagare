#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <string_view>

// What the shell_*.cpp files share with each other. Nothing else should include this.
namespace app::shell
{

/// Every name Nagare puts in the registry. --shell-test passes its own, so it can run on a real PC.
struct Identity
{
    std::wstring appName;
    std::wstring softwareKey;
    std::wstring torrentClass;
    std::wstring magnetClass;
    std::wstring torrentExtension;
};

[[nodiscard]] Identity const& liveIdentity();
[[nodiscard]] std::wstring executablePath();
[[nodiscard]] std::wstring capabilitiesKey(Identity const& identity);
[[nodiscard]] std::wstring classKey(std::wstring_view name);

[[nodiscard]] std::wstring readRegistryString(std::wstring const& key, wchar_t const* name);
[[nodiscard]] bool registryKeyExists(std::wstring const& key);

/// True if anything had to change.
bool writeRegistration(Identity const& identity, std::wstring const& executable);
void removeRegistration(Identity const& identity);

/// The Start with Windows value. An empty command removes it.
void setRunCommand(std::wstring const& valueName, std::wstring const& command);
[[nodiscard]] std::wstring runCommand(std::wstring const& valueName);
[[nodiscard]] wchar_t const* runKey();
[[nodiscard]] wchar_t const* registeredApplicationsKey();

/// Unix time of the last blocklist update, which outlives the process.
[[nodiscard]] int64_t blocklistUpdated();
void setBlocklistUpdated(int64_t unixTime);

/// Turns on writing to the registry for the live identity. Off by default.
void allowSystemChanges(bool allowed);

/// Lets later launches find this window, once it can take requests.
void publishInstance(HWND window);
void withdrawInstance(HWND window);
/// Asks a running Nagare to quit and waits for it.
void closeRunningInstance();

// The tray icon. The window keeps working if it can't be added.

void addTrayIcon(HWND window);
void removeTrayIcon();
/// True if `message` was the tray's.
bool handleTrayMessage(UINT message, WPARAM wparam, LPARAM lparam);
/// True in a full screen game, a presentation or Do Not Disturb.
[[nodiscard]] bool userWantsQuiet();
/// Shows a notification from the tray icon. Windows itself holds it back in Do Not Disturb.
void notify(std::wstring_view title, std::wstring_view body, bool sound);

} // namespace app::shell
