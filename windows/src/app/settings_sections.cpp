#include <windows.h>

#include <shellapi.h>

#include <array>
#include <cmath>
#include <cwchar>

#include "app/app.h"
#include "app/settings_internal.h"
#include "app/settings_panel.h"

namespace app
{

namespace
{

using namespace ui;

constexpr float kSourceTagHeight = 18.0F;
constexpr float kBlocklistFieldWidth = 300.0F;
constexpr TextStyle kSourceTag{ 12.0F, DWRITE_FONT_WEIGHT_NORMAL, false };

constexpr int kAtLeastNone = 0;
constexpr int kAtLeastOne = 1;
constexpr int kMaxSpeedKBps = 10'000'000;
// The engine keeps these in 16 bits.
constexpr int kMaxCount = 65535;
constexpr int kMaxMinutes = 65535;
constexpr int kMaxPort = 65535;

struct DayChoice
{
    wchar_t const* title;
    int days;
};

// tr_sched_day: Sunday is the lowest bit.
constexpr std::array<DayChoice, 3> kDayChoices{ {
    { L"Every day", 0b1111111 },
    { L"Weekdays", 0b0111110 },
    { L"Weekends", 0b1000001 },
} };

constexpr wchar_t const* kBuiltInSource = L"Internet Archive";

} // namespace

void SettingsPanel::buildGeneral()
{
    SettingsHooks const& hooks = settingsHooks();

    auto* appearance = body_->addRow(L"Appearance", L"Light paper or dark warm grey");
    auto* pills = appearance->add<PillBar>();
    pills->setPills({ { L"Match system" }, { L"Light" }, { L"Dark" } });
    pills->setSelected(static_cast<int>(Theme::get().mode()));
    pills->onChange = [this](int mode) { applyAppearance(mode); };
    appearance->fit(pills, pills->preferredWidth(), PillBar::kHeight);

    auto* defaultApp = body_->addRow(L"Default app", L"Open torrents and magnet links in Nagare");
    auto* makeDefault = addButton(defaultApp, L"Make Default", {});
    makeDefault->onClick = [makeDefault]
    {
        SettingsHooks const& current = settingsHooks();
        current.makeDefaultApp();
        // Windows may send the choice off to its own Settings app, so only its answer counts.
        makeDefault->setEnabled(!current.isDefaultApp || !current.isDefaultApp());
    };
    makeDefault->setEnabled(hooks.makeDefaultApp && !(hooks.isDefaultApp && hooks.isDefaultApp()));

    hookSwitchRow(L"Start with Windows", L"Open Nagare when you sign in", hooks.startsWithWindows, hooks.setStartsWithWindows);
    hookSwitchRow(L"Keep running in the tray", L"Closing the window leaves transfers going", hooks.keepsRunningInTray, hooks.setKeepsRunningInTray);

    switchRow(L"Keep the PC awake", L"Don't sleep while something is transferring", &Prefs::sleepPrevent);
    switchRow(L"Ask before removing", L"Confirm when a transfer is still active", &Prefs::checkRemove);
    switchRow(L"Ask before quitting", L"Confirm when transfers are still active", &Prefs::checkQuit);
    switchRow(L"Notifications", L"Say when a download finishes", &Prefs::displayNotifications);
    switchRow(L"Sound when downloaded", L"Play a sound as a download finishes", &Prefs::playDownloadSound);
    switchRow(L"Sound when seeded", L"Play a sound as seeding finishes", &Prefs::playSeedingSound);
}

void SettingsPanel::buildTransfers()
{
    FolderBinding saveTo;
    saveTo.read = [this] { return current_.downloadFolder; };
    saveTo.write = [this](std::wstring const& folder) { changeSettings([&folder](Settings& next) { next.downloadFolder = folder; }); };
    folderRow(L"Save to", L"Where new downloads go", std::move(saveTo));

    auto* ask = body_->addRow(L"Ask where to save", L"Show the add window for every torrent");
    addSwitch(
        ask,
        App::get().engine().prefs().downloadAsk,
        [this](bool on)
        {
            changePrefs(
                [on](Prefs& next)
                {
                    next.downloadAsk = on;
                    next.magnetOpenAsk = on;
                });
        });

    switchRow(L"Start right away", L"Begin as soon as a torrent is added", &Settings::startAddedTransfers);

    auto* ratio = body_->addRow(L"Seed until ratio", L"Then stop and call it finished");
    addRatioField(ratio);
    addSwitch(ratio, &Settings::ratioLimitOn);

    numberRow(L"Seed until idle", L"Stop once nobody has wanted anything for this long", &Settings::idleMinutes, { kAtLeastOne, kMaxMinutes }, L"min", &Settings::idleLimitOn);
    switchRow(L"Remove when seeded", L"Take a transfer off the list once it finishes", &Prefs::removeWhenFinishSeeding);
    switchRow(L"Open when done", L"Double-click a finished transfer to play it", &Prefs::openFinishedOnDoubleClick);

    body_->addHeading(L"Folders");

    FolderBinding incomplete;
    incomplete.read = [this] { return current_.incompleteFolder; };
    incomplete.write = [this](std::wstring const& folder) { changeSettings([&folder](Settings& next) { next.incompleteFolder = folder; }); };
    incomplete.isOn = [this] { return current_.incompleteFolderOn; };
    incomplete.setOn = [this](bool on) { changeSettings([on](Settings& next) { next.incompleteFolderOn = on; }); };
    folderRow(L"Keep unfinished files in", L"Until they finish downloading", std::move(incomplete));

    FolderBinding watch;
    watch.read = [] { return App::get().engine().prefs().watchFolder; };
    watch.write = [this](std::wstring const& folder) { changePrefs([&folder](Prefs& next) { next.watchFolder = folder; }); };
    watch.isOn = [] { return App::get().engine().prefs().watchFolderOn; };
    watch.setOn = [this](bool on) { changePrefs([on](Prefs& next) { next.watchFolderOn = on; }); };
    folderRow(L"Watch folder", L"Add any .torrent file that lands here", std::move(watch));

    body_->addHeading(L"Files");
    switchRow(L"Mark unfinished files", L"Add .part to their names until they're done", &Settings::renamePartialFiles);
    switchRow(L"Check files when done", L"Verify the data after it finishes downloading", &Settings::verifyOnCompletion);
    switchRow(L"Recycle .torrent files", L"Move them to the Recycle Bin once added", &Prefs::deleteOriginalTorrent);
    switchRow(L"Warn when space is low", L"Before adding something that won't fit", &Prefs::warningRemainingSpace);
}

void SettingsPanel::buildSpeed()
{
    NumberRange const speed{ kAtLeastNone, kMaxSpeedKBps };

    numberRow(L"Limit downloads", L"A ceiling for all transfers together", &Settings::downKBps, speed, L"KB/s", &Settings::limitDown);
    numberRow(L"Limit uploads", L"A ceiling for all transfers together", &Settings::upKBps, speed, L"KB/s", &Settings::limitUp);

    auto* slow = body_->addRow(L"Slow mode", L"Overrides the limits above while it's on");
    slowMode_ = addSwitch(slow, current_.altSpeedOn, [](bool on) { App::get().engine().setAltSpeed(on); });

    numberRow(L"Slow mode downloads", L"Speed while slow mode is on", &Settings::altDownKBps, speed, L"KB/s", nullptr);
    numberRow(L"Slow mode uploads", L"Speed while slow mode is on", &Settings::altUpKBps, speed, L"KB/s", nullptr);

    switchRow(L"Run on a schedule", L"Turn slow mode on and off by itself", &Settings::altScheduleOn);
    buildSchedule();
}

void SettingsPanel::buildSchedule()
{
    auto* row = body_->addRow(L"Schedule", L"When slow mode runs");

    std::vector<std::wstring> titles;
    int selected = 0;
    for (size_t i = 0; i < kDayChoices.size(); ++i)
    {
        titles.emplace_back(kDayChoices[i].title);
        if (kDayChoices[i].days == current_.altDays)
        {
            selected = static_cast<int>(i);
        }
    }
    auto* days = row->add<Dropdown>(std::move(titles), selected);
    days->onChange = [this](int index) { changeSettings([index](Settings& next) { next.altDays = kDayChoices[static_cast<size_t>(index)].days; }); };
    row->fit(days, days->preferredWidth(), Dropdown::kHeight);

    addTimeField(row, &Settings::altBeginMinutes);
    addTimeField(row, &Settings::altEndMinutes);
}

void SettingsPanel::buildSources()
{
    for (std::wstring const& name : App::get().engine().searchSourceNames())
    {
        auto* row = body_->addRow(name, {});
        auto* tag = row->add<Label>(name == kBuiltInSource ? L"Built in" : L"Added by you", kSourceTag, ColorRole::Foreground);
        row->fit(tag, std::ceil(tag->preferredWidth()), kSourceTagHeight);
    }

    auto* addSource = body_->addRow(L"Add a source", L"Sources are small JSON files, and nothing in them is run");
    addButton(
        addSource,
        L"Open Folder",
        []
        {
            std::wstring const folder = App::get().engine().searchSourcesFolder();
            ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        });
}

void SettingsPanel::buildAdvanced()
{
    NumberRange const queue{ kAtLeastNone, kMaxCount };
    NumberRange const peers{ kAtLeastOne, kMaxCount };

    body_->addHeading(L"Queue");
    numberRow(L"Downloads at once", L"Others wait their turn", &Settings::downloadQueueSize, queue, {}, &Settings::downloadQueueOn);
    numberRow(L"Seeding at once", L"Others wait their turn", &Settings::seedQueueSize, queue, {}, &Settings::seedQueueOn);
    numberRow(L"Skip stalled transfers", L"Stop counting a transfer that has gone quiet", &Settings::stalledMinutes, { kAtLeastOne, kMaxMinutes }, L"min", &Settings::stalledOn);

    body_->addHeading(L"Peers");
    numberRow(L"Most peers overall", L"Across all transfers", &Settings::peerLimitGlobal, peers, {}, nullptr);
    numberRow(L"Most peers per transfer", L"For each transfer", &Settings::peerLimitPerTorrent, peers, {}, nullptr);

    auto* encryption = body_->addRow(L"Encryption", L"Require can mean fewer peers");
    auto* mode = encryption->add<Dropdown>(std::vector<std::wstring>{ L"Allow", L"Prefer", L"Require" }, static_cast<int>(current_.encryption));
    mode->onChange = [this](int index) { changeSettings([index](Settings& next) { next.encryption = static_cast<engine::EncryptionMode>(index); }); };
    encryption->fit(mode, mode->preferredWidth(), Dropdown::kHeight);

    switchRow(L"DHT", L"Find peers without a tracker", &Settings::dht);
    switchRow(L"Peer exchange", L"Let peers introduce each other", &Settings::pex);
    switchRow(L"Local peers", L"Find peers on this network", &Settings::lpd);
    switchRow(L"µTP", L"A gentler transfer protocol", &Settings::utp);

    body_->addHeading(L"Connection");
    // A new port rebinds a socket, so unlike other numbers this one waits for Return or leaving the field.
    portRow_ = body_->addRow(L"Port", L"For incoming connections");
    addNumberField(portRow_, &Settings::peerPort, { kAtLeastOne, kMaxPort }, false);
    portTest_ = addButton(portRow_, L"Test", [this] { testPort(); });
    switchRow(L"New port each launch", L"Pick a random port on start", &Settings::randomPortOnStart);
    switchRow(L"Map the port automatically", L"Ask the router with UPnP or NAT-PMP", &Settings::portForwarding);

    buildBlocklist();
    buildImport();
}

void SettingsPanel::buildBlocklist()
{
    body_->addHeading(L"Blocklist");

    auto* use = body_->addRow(L"Use a blocklist", L"Refuse peers on a downloaded list of addresses");
    addSwitch(
        use,
        current_.blocklistOn,
        [this](bool on)
        {
            changeSettings([on](Settings& next) { next.blocklistOn = on; });
            refreshBlocklistButton();
        });

    auto* address = body_->addRow(L"Address", L"Where to download it from");
    auto* url = address->fit(address->add<TextField>(), kBlocklistFieldWidth, TextField::kHeight);
    url->setPlaceholder(L"https://");
    url->setText(current_.blocklistUrl);
    bindEdit(
        url,
        [this, url](bool)
        {
            if (url->text() != current_.blocklistUrl)
            {
                changeSettings([url](Settings& next) { next.blocklistUrl = url->text(); });
                refreshBlocklistButton();
            }
        },
        true);

    size_t const rules = App::get().engine().blocklistRuleCount();
    wchar_t status[64];
    swprintf_s(status, L"%zu IP address rules in list", rules);
    blocklistRow_ = body_->addRow(L"Update now", rules > 0 ? status : L"A blocklist must first be downloaded");
    blocklistUpdate_ = addButton(blocklistRow_, L"Update", [this] { updateBlocklist(); });
    refreshBlocklistButton();

    switchRow(L"Update automatically", L"Fetch a fresh copy every week", &Prefs::blocklistAutoUpdate);
}

void SettingsPanel::buildImport()
{
    body_->addHeading(L"qBittorrent");
    importRow_ = body_->addRow(L"Import", L"Add what it has on file, paused, without touching it");
    addButton(importRow_, L"Run Again", [this] { importAgain(); });
}

} // namespace app
