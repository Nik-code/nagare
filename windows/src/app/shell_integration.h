#pragma once

#include <windows.h>

#include <string>
#include <vector>

/// Everything that ties Nagare into Windows: one running instance, file and link handling, the tray,
/// notifications, sleep, and a clean end to the process. All of it runs on the UI thread.
namespace app::shell
{

// Launching

/// True for the demo and the build checks. They run side by side and must leave the machine alone.
[[nodiscard]] bool runsSideBySide(int argc, wchar_t** argv);
/// The torrent files and magnet links on a command line, with paths made absolute.
[[nodiscard]] std::vector<std::wstring> openRequests(int argc, wchar_t** argv);
/// Becomes the one running Nagare, or hands `requests` to the one already running and returns false.
[[nodiscard]] bool claimInstance(std::vector<std::wstring> const& requests);

// The installer runs these through --register and --unregister, with no window.

/// Offers Nagare to Windows for torrent files and magnet links. This takes nothing away from whatever
/// the user has now: it only adds Nagare to the list that Default Apps and Open With show.
void registerApp();
/// Closes a running Nagare and removes everything registerApp and the hooks below ever wrote.
void unregisterApp();

// Running

struct Options
{
    /// The tray icon, notifications, sleep prevention and the other things that follow the engine.
    bool tray = true;
    /// Allows registry writes and the default app question. Off, the hooks below only remember
    /// their state for this run, so a demo can flip them without touching the machine.
    bool system = true;
};

/// Call once, after App::start.
void start(Options options);
/// Takes the tray icon down and lets the machine sleep. Safe to call twice.
void stop();
/// For Window::onMessage. True if the message was handled.
bool handleMessage(UINT message, WPARAM wparam, LPARAM lparam);
/// Quits, after asking if transfers are running and the Settings say to ask.
void requestQuit();

// Hooks for the Settings screen.

[[nodiscard]] bool startsWithWindows();
void setStartsWithWindows(bool on);

/// Whether closing the window keeps Nagare running in the tray.
[[nodiscard]] bool keepsRunningInTray();
void setKeepsRunningInTray(bool on);

/// Whether Nagare opens both torrent files and magnet links right now.
[[nodiscard]] bool isDefaultApp();
/// Registers, then opens the Default Apps page, because Windows only lets the user make this change.
void makeDefaultApp();

// For the build checks.

void showDefaultPrompt();
void showQuitPrompt();
/// Runs the --shell-test checks in `folder`, under test names. Returns the process exit code.
int runShellTest(std::wstring const& folder);

} // namespace app::shell
