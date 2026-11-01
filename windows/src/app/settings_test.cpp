#include <windows.h>

#include <dwmapi.h>

#include <cmath>
#include <cstdio>
#include <cwchar>
#include <deque>
#include <functional>
#include <string>
#include <utility>

#include "app/app.h"
#include "app/settings_internal.h"
#include "app/settings_panel.h"

namespace app
{

namespace
{

using namespace ui;
using engine::AppPrefs;
using engine::SessionSettings;

constexpr DWORD kSettleMs = 260;
constexpr DWORD kPastDebounceMs = 700;
constexpr DWORD kSafetyMs = 30000;
constexpr int kFailedExit = 1;
constexpr int kNeedsDemoExit = 2;
constexpr int kHungExit = 99;

int gFailures = 0;
int gChecks = 0;
FILE* gLog = nullptr;
SettingsPanel* gPanel = nullptr;
size_t gBaselineWork = 0;
size_t gLowestWork = 0;
std::deque<std::pair<DWORD, std::function<void()>>> gSteps;

// What the shell hooks would hold, and how often they were asked to change it.
bool gStartsWithWindows = false;
bool gKeepsRunningInTray = false;
int gMadeDefault = 0;
std::wstring gNextFolder;

engine::Engine& engine()
{
    return App::get().engine();
}

void note(char const* line)
{
    if (gLog != nullptr)
    {
        fputs(line, gLog);
        fputc('\n', gLog);
        fflush(gLog);
    }
}

void check(bool passed, char const* name)
{
    ++gChecks;
    gFailures += passed ? 0 : 1;
    char line[160];
    sprintf_s(line, "%s %s", passed ? "ok  " : "FAIL", name);
    note(line);
}

void finish()
{
    char line[64];
    sprintf_s(line, "%d checks, %d failures", gChecks, gFailures);
    note(line);
    Window::get().quit(gFailures == 0 ? 0 : kFailedExit);
}

void then(DWORD delayMs, std::function<void()> step)
{
    gSteps.emplace_back(delayMs, std::move(step));
}

void runNext()
{
    if (gSteps.empty())
    {
        finish();
        return;
    }
    auto [delay, step] = std::move(gSteps.front());
    gSteps.pop_front();
    Window::get().after(
        delay,
        [step = std::move(step)]
        {
            step();
            runNext();
        });
}

template<typename T>
T* controlIn(SettingsRow* row, size_t nth = 0)
{
    for (auto const& child : row->children())
    {
        if (auto* found = dynamic_cast<T*>(child.get()); found != nullptr && nth-- == 0)
        {
            return found;
        }
    }
    return nullptr;
}

template<typename T>
T* control(wchar_t const* title, size_t nth = 0)
{
    SettingsRow* row = gPanel->findRow(title);
    return row != nullptr ? controlIn<T>(row, nth) : nullptr;
}

/// Works anywhere in the body, whether or not that part is scrolled into view.
void press(View* view)
{
    MouseEvent event;
    event.point = D2D1::Point2F(view->width() / 2.0F, view->height() / 2.0F);
    event.kind = MouseKind::Down;
    view->onMouse(event);
    event.kind = MouseKind::Up;
    view->onMouse(event);
}

void clickAt(float x, float y)
{
    float const scale = Window::get().renderer().dpi() / 96.0F;
    LPARAM const point = MAKELPARAM(static_cast<int>(x * scale + 0.5F), static_cast<int>(y * scale + 0.5F));
    HWND const window = Window::get().handle();
    SendMessageW(window, WM_MOUSEMOVE, 0, point);
    SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, point);
    SendMessageW(window, WM_LBUTTONUP, 0, point);
}

void clickView(View* view)
{
    auto const origin = view->absoluteOrigin();
    clickAt(origin.x + view->width() / 2.0F, origin.y + view->height() / 2.0F);
}

void wheelOver(View* view, int notches)
{
    float const scale = Window::get().renderer().dpi() / 96.0F;
    auto const origin = view->absoluteOrigin();
    POINT screen{ static_cast<LONG>((origin.x + view->width() / 2.0F) * scale), static_cast<LONG>((origin.y + view->height() / 2.0F) * scale) };
    ClientToScreen(Window::get().handle(), &screen);
    SendMessageW(Window::get().handle(), WM_MOUSEWHEEL, MAKEWPARAM(0, notches * WHEEL_DELTA), MAKELPARAM(screen.x, screen.y));
}

void clickPill(PillBar* bar, int index)
{
    auto const origin = bar->absoluteOrigin();
    D2D1_RECT_F const pill = bar->pillRect(index);
    clickAt(origin.x + (pill.left + pill.right) / 2.0F, origin.y + (pill.top + pill.bottom) / 2.0F);
}

void pressKey(UINT virtualKey)
{
    SendMessageW(Window::get().handle(), WM_KEYDOWN, virtualKey, 0);
    SendMessageW(Window::get().handle(), WM_KEYUP, virtualKey, 0);
}

void typeInto(TextField* field, std::wstring_view text)
{
    Window::get().focus(field);
    field->selectAll();
    if (text.empty())
    {
        pressKey(VK_DELETE);
    }
    for (wchar_t const character : text)
    {
        SendMessageW(Window::get().handle(), WM_CHAR, character, 0);
    }
}

void blur()
{
    Window::get().focus(nullptr);
}

/// Picks an item through the menu the dropdown really opens.
void choose(Dropdown* dropdown, int index)
{
    press(dropdown);
    for (int i = 0; i <= index; ++i)
    {
        pressKey(VK_DOWN);
    }
    pressKey(VK_RETURN);
}

bool sameRatio(double a, double b)
{
    return std::abs(a - b) < 0.0001;
}

void flipsSetting(wchar_t const* title, bool SessionSettings::* field, char const* name)
{
    auto* toggle = control<Switch>(title);
    check(toggle != nullptr && toggle->on() == engine().settings().*field, name);
    if (toggle == nullptr)
    {
        return;
    }
    bool const before = engine().settings().*field;
    press(toggle);
    check(engine().settings().*field == !before, name);
    press(toggle);
    check(engine().settings().*field == before, name);
}

void flipsPref(wchar_t const* title, bool AppPrefs::* field, char const* name)
{
    auto* toggle = control<Switch>(title);
    check(toggle != nullptr && toggle->on() == engine().prefs().*field, name);
    if (toggle == nullptr)
    {
        return;
    }
    bool const before = engine().prefs().*field;
    press(toggle);
    check(engine().prefs().*field == !before, name);
    press(toggle);
    check(engine().prefs().*field == before, name);
}

// The swell's own timer comes and goes while the demo moves, so the lowest reading is the fair one.
void sampleWork()
{
    gLowestWork = SIZE_MAX;
    for (int i = 0; i < 8; ++i)
    {
        then(25, [] { gLowestWork = std::min(gLowestWork, Window::get().pendingWork()); });
    }
}

void testGeneral()
{
    then(kSettleMs, []
    {
        // main.cpp has already wired the real hooks, and this test starts from none.
        settingsHooks() = {};
        gBaselineWork = Window::get().pendingWork();
        gPanel = Panel::open<SettingsPanel>();
        check(control<Switch>(L"Start with Windows") != nullptr && !control<Switch>(L"Start with Windows")->enabled(), "hooks: a row with no hook is disabled");
        check(control<Button>(L"Default app") != nullptr && !control<Button>(L"Default app")->enabled(), "hooks: no default-app hook, no button");

        SettingsHooks& hooks = settingsHooks();
        hooks.startsWithWindows = [] { return gStartsWithWindows; };
        hooks.setStartsWithWindows = [](bool on) { gStartsWithWindows = on; };
        hooks.keepsRunningInTray = [] { return gKeepsRunningInTray; };
        hooks.setKeepsRunningInTray = [](bool on) { gKeepsRunningInTray = on; };
        hooks.isDefaultApp = [] { return gMadeDefault > 0; };
        hooks.makeDefaultApp = [] { ++gMadeDefault; };
        hooks.pickFolder = [](std::wstring const&) { return gNextFolder; };
        gPanel->showSection(SettingsSection::General);
    });
    then(kSettleMs, []
    {
        auto* pills = control<PillBar>(L"Appearance");
        check(pills != nullptr, "appearance: pills exist");
        if (pills == nullptr)
        {
            return;
        }
        auto const darkFrame = []
        {
            BOOL dark = FALSE;
            DwmGetWindowAttribute(Window::get().handle(), DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
            return dark != FALSE;
        };
        clickPill(pills, 1);
        check(engine().prefs().themeMode == 1 && Theme::get().mode() == ThemeMode::Light && !Theme::get().dark(), "appearance: Light");
        check(!darkFrame(), "appearance: Light reaches the window frame");
        clickPill(pills, 2);
        check(engine().prefs().themeMode == 2 && Theme::get().mode() == ThemeMode::Dark && Theme::get().dark(), "appearance: Dark");
        check(darkFrame(), "appearance: Dark reaches the window frame");
        clickPill(pills, 0);
        check(engine().prefs().themeMode == 0 && Theme::get().mode() == ThemeMode::System, "appearance: Match system");
        check(darkFrame() == Theme::get().dark(), "appearance: the frame follows the system");
        clickPill(pills, 2);
    });
    then(kSettleMs, []
    {
        auto* start = control<Switch>(L"Start with Windows");
        auto* tray = control<Switch>(L"Keep running in the tray");
        auto* make = control<Button>(L"Default app");
        check(start->enabled() && tray->enabled() && make->enabled(), "hooks: rows come alive");
        press(start);
        check(gStartsWithWindows, "hooks: start with Windows is called");
        press(tray);
        check(gKeepsRunningInTray, "hooks: tray is called");
        press(make);
        check(gMadeDefault == 1 && !make->enabled(), "hooks: make default is called, then the button rests");

        // The rest press controls directly, so this one proves a real click gets through the panel.
        bool const removing = engine().prefs().checkRemove;
        clickView(control<Switch>(L"Ask before removing"));
        check(engine().prefs().checkRemove == !removing, "input: a real click reaches a row's switch");
        clickView(control<Switch>(L"Ask before removing"));

        flipsPref(L"Keep the PC awake", &AppPrefs::sleepPrevent, "prefs: keep awake");
        flipsPref(L"Ask before removing", &AppPrefs::checkRemove, "prefs: ask before removing");
        flipsPref(L"Ask before quitting", &AppPrefs::checkQuit, "prefs: ask before quitting");
        flipsPref(L"Notifications", &AppPrefs::displayNotifications, "prefs: notifications");
        flipsPref(L"Sound when downloaded", &AppPrefs::playDownloadSound, "prefs: download sound");
        flipsPref(L"Sound when seeded", &AppPrefs::playSeedingSound, "prefs: seeding sound");
    });
}

void testTransfers()
{
    then(kSettleMs, []
    {
        clickPill(gPanel->sectionPills(), static_cast<int>(SettingsSection::Transfers));
        check(gPanel->findRow(L"Save to") != nullptr, "sections: a pill click shows Transfers");

        auto* change = control<Button>(L"Save to");
        gNextFolder = L"C:\\Demo\\Movies";
        press(change);
        check(engine().settings().downloadFolder == gNextFolder, "folder: save to");
        check(control<Label>(L"Save to")->text() == gNextFolder, "folder: the row shows it");
        gNextFolder.clear();
        press(change);
        check(engine().settings().downloadFolder == L"C:\\Demo\\Movies", "folder: cancelling changes nothing");

        auto* ask = control<Switch>(L"Ask where to save");
        bool const asked = engine().prefs().downloadAsk;
        press(ask);
        check(engine().prefs().downloadAsk == !asked && engine().prefs().magnetOpenAsk == !asked, "prefs: asking covers torrents and magnets");
        press(ask);

        flipsSetting(L"Start right away", &SessionSettings::startAddedTransfers, "settings: start right away");
        flipsSetting(L"Seed until ratio", &SessionSettings::ratioLimitOn, "settings: ratio switch");
        flipsSetting(L"Seed until idle", &SessionSettings::idleLimitOn, "settings: idle switch");
        flipsPref(L"Remove when seeded", &AppPrefs::removeWhenFinishSeeding, "prefs: remove when seeded");
        flipsPref(L"Open when done", &AppPrefs::openFinishedOnDoubleClick, "prefs: open when done");
        flipsSetting(L"Mark unfinished files", &SessionSettings::renamePartialFiles, "settings: mark unfinished");
        flipsSetting(L"Check files when done", &SessionSettings::verifyOnCompletion, "settings: verify");
        flipsPref(L"Recycle .torrent files", &AppPrefs::deleteOriginalTorrent, "prefs: recycle torrents");
        flipsPref(L"Warn when space is low", &AppPrefs::warningRemainingSpace, "prefs: warn on space");

        auto settings = engine().settings();
        settings.incompleteFolder.clear();
        settings.incompleteFolderOn = false;
        engine().applySettings(settings);
        gPanel->showSection(SettingsSection::Transfers);

        auto* unfinished = control<Switch>(L"Keep unfinished files in");
        press(unfinished);
        check(!unfinished->on() && !engine().settings().incompleteFolderOn, "folder: backing out of the dialog turns the switch off");
        gNextFolder = L"C:\\Demo\\Partial";
        press(unfinished);
        check(engine().settings().incompleteFolderOn && engine().settings().incompleteFolder == gNextFolder, "folder: incomplete");

        auto* watch = control<Switch>(L"Watch folder");
        gNextFolder.clear();
        press(watch);
        check(!engine().prefs().watchFolderOn, "folder: watch needs a folder");
        gNextFolder = L"C:\\Demo\\Watch";
        press(watch);
        check(engine().prefs().watchFolderOn && engine().prefs().watchFolder == gNextFolder, "folder: watch");

        typeInto(control<TextField>(L"Seed until ratio"), L"3.5");
        check(!sameRatio(engine().settings().ratioLimit, 3.5), "debounce: typing doesn't apply straight away");
    });
    then(kPastDebounceMs, []
    {
        check(sameRatio(engine().settings().ratioLimit, 3.5), "debounce: a pause applies it");
        auto* ratio = control<TextField>(L"Seed until ratio");
        typeInto(ratio, L"0");
        pressKey(VK_RETURN);
        check(sameRatio(engine().settings().ratioLimit, 0.1) && ratio->text() == L"0.1", "number: Return applies, and the ratio floor holds");
        typeInto(ratio, L"");
        blur();
        check(sameRatio(engine().settings().ratioLimit, 0.1) && ratio->text() == L"0.1", "number: an empty field goes back");
        typeInto(ratio, L"2abc.5");
        blur();
        check(sameRatio(engine().settings().ratioLimit, 2.5), "number: the filter drops letters");

        typeInto(control<TextField>(L"Seed until idle"), L"45");
        blur();
        check(engine().settings().idleMinutes == 45, "number: idle minutes apply on blur");
    });
}

void testSpeed()
{
    then(kSettleMs, []
    {
        clickPill(gPanel->sectionPills(), static_cast<int>(SettingsSection::Speed));

        flipsSetting(L"Limit downloads", &SessionSettings::limitDown, "settings: limit downloads");
        flipsSetting(L"Limit uploads", &SessionSettings::limitUp, "settings: limit uploads");
        flipsSetting(L"Run on a schedule", &SessionSettings::altScheduleOn, "settings: schedule");

        auto const number = [](wchar_t const* title, std::wstring_view text, int SessionSettings::* field, int expected, char const* name)
        {
            typeInto(control<TextField>(title), text);
            blur();
            check(engine().settings().*field == expected, name);
        };
        number(L"Limit downloads", L"1234", &SessionSettings::downKBps, 1234, "number: download limit");
        number(L"Limit uploads", L"321", &SessionSettings::upKBps, 321, "number: upload limit");
        number(L"Slow mode downloads", L"77", &SessionSettings::altDownKBps, 77, "number: slow down");
        number(L"Slow mode uploads", L"66", &SessionSettings::altUpKBps, 66, "number: slow up");

        auto* slow = control<Switch>(L"Slow mode");
        bool const before = engine().settings().altSpeedOn;
        press(slow);
        check(engine().settings().altSpeedOn == !before, "slow mode: goes through the engine");

        auto* schedule = gPanel->findRow(L"Schedule");
        auto* days = controlIn<Dropdown>(schedule);
        choose(days, 1);
        check(engine().settings().altDays == 0b0111110, "dropdown: weekdays");
        choose(days, 2);
        check(engine().settings().altDays == 0b1000001, "dropdown: weekends");
        choose(days, 0);
        check(engine().settings().altDays == 0b1111111, "dropdown: every day");

        auto* from = controlIn<TextField>(schedule, 0);
        auto* to = controlIn<TextField>(schedule, 1);
        typeInto(from, L"7:30");
        blur();
        check(engine().settings().altBeginMinutes == 450 && from->text() == L"07:30", "time: applies and tidies");
        typeInto(to, L"23:05");
        pressKey(VK_RETURN);
        check(engine().settings().altEndMinutes == 1385, "time: Return applies");
        typeInto(from, L"25:00");
        blur();
        check(engine().settings().altBeginMinutes == 450 && from->text() == L"07:30", "time: nonsense goes back");
    });
}

void testSources()
{
    then(kSettleMs, []
    {
        clickPill(gPanel->sectionPills(), static_cast<int>(SettingsSection::Sources));
        auto* tag = control<Label>(L"Internet Archive");
        check(tag != nullptr && tag->text() == L"Built in", "sources: the built-in one is listed");
        check(control<Button>(L"Add a source") != nullptr, "sources: the folder button is there");
    });
}

void testAdvanced()
{
    then(kSettleMs, []
    {
        clickPill(gPanel->sectionPills(), static_cast<int>(SettingsSection::Advanced));

        flipsSetting(L"Downloads at once", &SessionSettings::downloadQueueOn, "settings: download queue");
        flipsSetting(L"Seeding at once", &SessionSettings::seedQueueOn, "settings: seed queue");
        flipsSetting(L"Skip stalled transfers", &SessionSettings::stalledOn, "settings: stalled");
        flipsSetting(L"DHT", &SessionSettings::dht, "settings: DHT");
        flipsSetting(L"Peer exchange", &SessionSettings::pex, "settings: PEX");
        flipsSetting(L"Local peers", &SessionSettings::lpd, "settings: LPD");
        flipsSetting(L"µTP", &SessionSettings::utp, "settings: uTP");
        flipsSetting(L"New port each launch", &SessionSettings::randomPortOnStart, "settings: random port");
        flipsSetting(L"Map the port automatically", &SessionSettings::portForwarding, "settings: port mapping");

        auto const number = [](wchar_t const* title, std::wstring_view text, int SessionSettings::* field, int expected, char const* name)
        {
            typeInto(control<TextField>(title), text);
            blur();
            check(engine().settings().*field == expected, name);
        };
        number(L"Downloads at once", L"7", &SessionSettings::downloadQueueSize, 7, "number: download queue");
        number(L"Seeding at once", L"9", &SessionSettings::seedQueueSize, 9, "number: seed queue");
        number(L"Skip stalled transfers", L"15", &SessionSettings::stalledMinutes, 15, "number: stalled minutes");
        number(L"Most peers overall", L"400", &SessionSettings::peerLimitGlobal, 400, "number: peers overall");
        number(L"Most peers per transfer", L"80", &SessionSettings::peerLimitPerTorrent, 80, "number: peers per transfer");
        number(L"Most peers overall", L"99999", &SessionSettings::peerLimitGlobal, 65535, "number: clamped to what the engine stores");

        auto* encryption = control<Dropdown>(L"Encryption");
        choose(encryption, 2);
        check(engine().settings().encryption == engine::EncryptionMode::Required, "dropdown: encryption required");
        choose(encryption, 0);
        check(engine().settings().encryption == engine::EncryptionMode::Allowed, "dropdown: encryption allowed");

        typeInto(control<TextField>(L"Port"), L"6881");
        wheelOver(gPanel->scroller(), -3);
    });
    then(kPastDebounceMs, []
    {
        check(gPanel->scroller()->scrollOffset() > 0.0F, "input: the wheel scrolls the body");
        check(engine().settings().peerPort != 6881, "port: no rebind on a pause in typing");
        auto* port = control<TextField>(L"Port");
        pressKey(VK_RETURN);
        check(engine().settings().peerPort == 6881, "port: Return applies it");
        typeInto(port, L"70000");
        blur();
        check(engine().settings().peerPort == 65535 && port->text() == L"65535", "port: clamped");

        press(control<Button>(L"Port"));
        check(gPanel->findRow(L"Port")->hint() == L"Checking the port" && !control<Button>(L"Port")->enabled(), "port test: says it's checking");
    });
    then(kSettleMs, []
    {
        check(gPanel->findRow(L"Port")->hint() == L"Port is open" && control<Button>(L"Port")->enabled(), "port test: reports the answer");

        auto* update = control<Button>(L"Update now");
        check(!update->enabled(), "blocklist: nothing to update from yet");
        flipsSetting(L"Use a blocklist", &SessionSettings::blocklistOn, "settings: blocklist switch");
        press(control<Switch>(L"Use a blocklist"));
        typeInto(control<TextField>(L"Address"), L"https://example.com/list.gz");
        blur();
        check(engine().settings().blocklistUrl == L"https://example.com/list.gz", "blocklist: address");
        check(update->enabled(), "blocklist: on with an address, the button wakes");
        press(update);
        check(gPanel->findRow(L"Update now")->hint() == L"Downloading" && !update->enabled(), "blocklist: says it's downloading");
    });
    then(kSettleMs, []
    {
        check(gPanel->findRow(L"Update now")->hint() == L"0 IP address rules in list" && control<Button>(L"Update now")->enabled(), "blocklist: reports the count");
        flipsPref(L"Update automatically", &AppPrefs::blocklistAutoUpdate, "prefs: blocklist auto update");

        press(control<Button>(L"Import"));
        check(gPanel->findRow(L"Import")->hint() == L"Nothing new to add", "import: reports the result");
    });
}

void testClosing()
{
    then(kSettleMs, []
    {
        clickPill(gPanel->sectionPills(), static_cast<int>(SettingsSection::Speed));
        typeInto(control<TextField>(L"Limit downloads"), L"555");
        check(engine().settings().downKBps != 555, "closing: not applied yet");
        pressKey(VK_ESCAPE);
        check(engine().settings().downKBps == 555, "closing: Esc applies what was typed");
    });
    then(kSettleMs, []
    {
        check(Window::get().panel() == nullptr, "closing: Esc closes");
    });
    sampleWork();
    then(0, []
    {
        check(gLowestWork <= gBaselineWork, "closing: nothing left running");

        gPanel = Panel::open<SettingsPanel>(SettingsSection::Speed);
        check(control<TextField>(L"Limit downloads")->text() == L"555", "reopen: shows what was saved");
        typeInto(control<TextField>(L"Limit uploads"), L"444");
        Window::get().setPanel(nullptr);
        gPanel = nullptr;
    });
    then(kSettleMs, []
    {
        check(engine().settings().upKBps == 444, "closing: losing the panel applies what was typed");
    });
    sampleWork();
    then(0, []
    {
        check(gLowestWork <= gBaselineWork, "closing: the debounce went with it");

        gPanel = Panel::open<SettingsPanel>(SettingsSection::Advanced);
        press(control<Button>(L"Port"));
        Window::get().setPanel(nullptr);
        gPanel = nullptr;
    });
    then(kSettleMs, []
    {
        check(true, "closing: a port test can finish after the panel has gone");
    });
}

} // namespace

void runSettingsTest()
{
    Window& window = Window::get();
    if (wcsstr(GetCommandLineW(), L"--demo") == nullptr)
    {
        // These flip every setting there is, so never on someone's real config.
        window.quit(kNeedsDemoExit);
        return;
    }

    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring logPath = path;
    logPath.resize(logPath.find_last_of(L'\\') + 1);
    logPath += L"settings-test.log";
    _wfopen_s(&gLog, logPath.c_str(), L"w");

    testGeneral();
    testTransfers();
    testSpeed();
    testSources();
    testAdvanced();
    testClosing();

    // A safety net: a test that hangs still has to end the run with a failure.
    window.after(kSafetyMs, [] { Window::get().quit(kHungExit); });
    runNext();
}

} // namespace app
