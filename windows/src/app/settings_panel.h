#pragma once

#include <functional>
#include <string>

namespace app
{

/// What the Settings panel can't do on its own. Whoever owns the shell integration fills these in
/// before the panel opens. A row whose hook is missing shows disabled, rather than pretending.
struct SettingsHooks
{
    std::function<bool()> startsWithWindows;
    std::function<void(bool)> setStartsWithWindows;

    std::function<bool()> keepsRunningInTray;
    std::function<void(bool)> setKeepsRunningInTray;

    std::function<bool()> isDefaultApp;
    std::function<void()> makeDefaultApp;

    /// Gets the current folder and returns the new one, or nothing if cancelled. Left unset, the
    /// system's folder dialog opens, so only the tests fill this in.
    std::function<std::wstring(std::wstring const&)> pickFolder;
};

SettingsHooks& settingsHooks();

/// --settings-test: needs --demo, drives every kind of control and quits non-zero on a failure.
void runSettingsTest();

} // namespace app
