#include "app/shell_integration.h"

#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <vector>

#include "app/app.h"
#include "app/shell_internal.h"
#include "ui/widgets.h"
#include "ui/window.h"

namespace app::shell
{

namespace
{

using engine::Event;
using engine::EventType;
using engine::TransferId;
using engine::TransferInfo;
using engine::TransferState;

constexpr DWORD kStartupSettleMs = 4000;
constexpr DWORD kDefaultPromptDelayMs = 1500;
constexpr ULONGLONG kDiskCheckIntervalMs = 60'000;
constexpr DWORD kBlocklistFirstCheckMs = 30'000;
constexpr DWORD kBlocklistCheckIntervalMs = 60U * 60U * 1000U;
constexpr int64_t kBlocklistMinimumSeconds = 24 * 60 * 60;

constexpr float kPromptWidth = 480.0F;
constexpr float kPromptHeight = 176.0F;
constexpr float kPromptInset = 28.0F;
constexpr float kPromptButtonWidth = 110.0F;
constexpr ui::TextStyle kPromptTitle{ 22.0F, DWRITE_FONT_WEIGHT_MEDIUM, true };
constexpr ui::TextStyle kPromptBody{ 13.0F, DWRITE_FONT_WEIGHT_NORMAL, false };

/// A short question in the app's own glass. Only ever one, and it can replace nothing important.
class PromptPanel final : public ui::Panel
{
public:
    PromptPanel(std::wstring const& title, std::wstring const& firstLine, std::wstring const& secondLine, std::wstring const& cancel, std::wstring const& confirm, std::function<void()> onConfirm)
        : Panel(kPromptWidth, kPromptHeight)
    {
        title_ = add<ui::Label>(title, kPromptTitle, ui::ColorRole::Bright);
        firstLine_ = add<ui::Label>(firstLine, kPromptBody, ui::ColorRole::Foreground);
        secondLine_ = add<ui::Label>(secondLine, kPromptBody, ui::ColorRole::Foreground);
        cancel_ = add<ui::Button>(cancel);
        cancel_->setCapsule(true);
        cancel_->onClick = [this] { close(); };
        confirm_ = add<ui::Button>(confirm, ui::ButtonStyle::Accent);
        confirm_->onClick = [this, onConfirm = std::move(onConfirm)]
        {
            auto const run = onConfirm;
            close();
            run();
        };
    }

protected:
    void layoutContent() override
    {
        float const right = width() - kPromptInset;
        float const buttonTop = height() - kPromptInset - ui::Button::kLargeHeight;
        title_->setFrame(D2D1::RectF(kPromptInset, 24.0F, right, 56.0F));
        firstLine_->setFrame(D2D1::RectF(kPromptInset, 62.0F, right, 82.0F));
        secondLine_->setFrame(D2D1::RectF(kPromptInset, 82.0F, right, 102.0F));
        confirm_->setFrame(D2D1::RectF(right - kPromptButtonWidth, buttonTop, right, buttonTop + ui::Button::kLargeHeight));
        cancel_->setFrame(D2D1::RectF(right - 2.0F * kPromptButtonWidth - 12.0F, buttonTop, right - kPromptButtonWidth - 12.0F, buttonTop + ui::Button::kLargeHeight));
    }

private:
    ui::Label* title_ = nullptr;
    ui::Label* firstLine_ = nullptr;
    ui::Label* secondLine_ = nullptr;
    ui::Button* cancel_ = nullptr;
    ui::Button* confirm_ = nullptr;
};

Options options;
bool running = false;
int refreshListener = 0;
int eventListener = 0;
int blocklistTimer = 0;
bool preventingSleep = false;
ULONGLONG nextDiskCheck = 0;
std::vector<TransferId> warnedLowSpace;

[[nodiscard]] TransferInfo const* findTransfer(TransferId id)
{
    auto const& transfers = App::get().transfers();
    auto const found = std::lower_bound(transfers.begin(), transfers.end(), id, [](TransferInfo const& info, TransferId wanted) { return info.id < wanted; });
    return found != transfers.end() && found->id == id ? &*found : nullptr;
}

struct Activity
{
    int active = 0;
    int downloading = 0;
};

/// The same count the Mac makes before it asks about quitting.
[[nodiscard]] Activity countActivity()
{
    Activity activity;
    for (TransferInfo const& transfer : App::get().transfers())
    {
        bool const moving = transfer.state == TransferState::Download || transfer.state == TransferState::Fetching || transfer.state == TransferState::Verifying
            || transfer.state == TransferState::Seeding;
        if (moving && !transfer.isStalled)
        {
            ++activity.active;
            activity.downloading += transfer.leftBytes > 0 ? 1 : 0;
        }
    }
    return activity;
}

void presentPrompt(std::unique_ptr<PromptPanel> prompt)
{
    ui::Window& window = ui::Window::get();
    if (dynamic_cast<PromptPanel*>(window.panel()) != nullptr)
    {
        return;
    }
    ui::Panel::present(std::move(prompt));
}

void updateSleep()
{
    bool const wanted = App::get().engine().prefs().sleepPrevent && App::get().engine().sessionStats().anyWorking;
    if (wanted != preventingSleep)
    {
        preventingSleep = wanted;
        SetThreadExecutionState(wanted ? ES_CONTINUOUS | ES_SYSTEM_REQUIRED : ES_CONTINUOUS);
    }
}

void warnLowSpace(TransferInfo const& transfer)
{
    std::wstring const body = L"\"" + transfer.name + L"\" needs more room than the drive has left.";
    notify(L"Not enough disk space", body, true);
}

/// Only what is downloading right now counts, and each transfer warns once until it has room again.
void checkDiskSpace()
{
    ULONGLONG const now = GetTickCount64();
    if (now < nextDiskCheck)
    {
        return;
    }
    nextDiskCheck = now + kDiskCheckIntervalMs;

    engine::Engine& engine = App::get().engine();
    if (!engine.prefs().warningRemainingSpace)
    {
        return;
    }
    for (TransferInfo const& transfer : App::get().transfers())
    {
        if (transfer.state != TransferState::Download || transfer.leftBytes == 0)
        {
            continue;
        }
        int64_t const available = engine.freeDiskSpace(engine.details(transfer.id).currentFolder);
        auto const warned = std::find(warnedLowSpace.begin(), warnedLowSpace.end(), transfer.id);
        bool const short_ = available >= 0 && static_cast<uint64_t>(available) < transfer.leftBytes;
        if (short_ && warned == warnedLowSpace.end())
        {
            warnedLowSpace.push_back(transfer.id);
            warnLowSpace(transfer);
        }
        else if (!short_ && warned != warnedLowSpace.end())
        {
            warnedLowSpace.erase(warned);
        }
    }
}

void onEngineEvent(Event const& event)
{
    auto const& prefs = App::get().engine().prefs();
    std::wstring title;
    std::wstring body;
    bool sound = false;

    switch (event.type)
    {
    case EventType::DownloadFinished:
        title = L"Download Complete";
        sound = prefs.playDownloadSound;
        if (!event.flag)
        {
            return;
        }
        break;
    case EventType::SeedingFinished:
        title = L"Seeding Complete";
        sound = prefs.playSeedingSound;
        break;
    case EventType::AltSpeedChanged:
        if (event.flag2)
        {
            return;
        }
        title = event.flag ? L"Speed Limit Auto Enabled" : L"Speed Limit Auto Disabled";
        body = L"Bandwidth settings changed";
        break;
    case EventType::WatchFolderAdded:
        title = L"Torrent File Auto Added";
        body = event.text;
        break;
    default:
        return;
    }

    if (body.empty())
    {
        TransferInfo const* const transfer = findTransfer(event.id);
        body = transfer != nullptr ? transfer->name : L"Nagare";
    }
    if (prefs.displayNotifications)
    {
        notify(title, body, sound);
    }
    else if (sound && !userWantsQuiet())
    {
        MessageBeep(MB_OK);
    }
}

/// Runs hourly so a change in Settings soon takes effect. A day is the shortest gap between updates.
void checkBlocklist()
{
    engine::Engine& engine = App::get().engine();
    if (!engine.prefs().blocklistAutoUpdate)
    {
        return;
    }
    auto const settings = engine.settings();
    int64_t const now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    if (!settings.blocklistOn || settings.blocklistUrl.empty() || now - blocklistUpdated() < kBlocklistMinimumSeconds)
    {
        return;
    }
    setBlocklistUpdated(now);
    engine.updateBlocklist([](bool, std::wstring const&) {});
}

void askToBeDefault()
{
    ui::Window& window = ui::Window::get();
    engine::Engine& engine = App::get().engine();
    // A panel in the way, or a launch at sign-in, just puts the question off to the next launch.
    if (engine.prefs().askedToBeDefault || window.panel() != nullptr || !window.visible())
    {
        return;
    }
    engine::AppPrefs prefs = engine.prefs();
    prefs.askedToBeDefault = true;
    engine.setPrefs(prefs);
    if (!isDefaultApp())
    {
        showDefaultPrompt();
    }
}

void handleClose()
{
    if (options.tray && keepsRunningInTray())
    {
        ui::Window::get().hideToTray();
    }
    else
    {
        requestQuit();
    }
}

/// Windows is ending the session or, for an installer, this process. Either way nothing runs after
/// this returns, so everything has to be saved inside the message.
void endSession()
{
    ui::Window& window = ui::Window::get();
    window.quit();
    stop();
    App::get().shutdown();
    ShutdownBlockReasonDestroy(window.handle());
}

} // namespace

void start(Options const startOptions)
{
    options = startOptions;
    running = true;
    allowSystemChanges(options.system);

    ui::Window& window = ui::Window::get();
    window.onCloseRequest = []
    {
        handleClose();
        return false;
    };
    publishInstance(window.handle());

    // What startup touched only once needn't stay in memory. Anything still in use comes straight back.
    window.after(kStartupSettleMs, [] { SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1)); });

    if (options.system)
    {
        registerApp();
        window.after(kDefaultPromptDelayMs, askToBeDefault);
    }
    if (!options.tray)
    {
        return;
    }

    addTrayIcon(window.handle());
    refreshListener = App::get().addRefreshListener(
        []
        {
            updateSleep();
            checkDiskSpace();
        });
    eventListener = App::get().addEventListener(onEngineEvent);
    window.after(kBlocklistFirstCheckMs, checkBlocklist);
    blocklistTimer = window.every(kBlocklistCheckIntervalMs, checkBlocklist);
}

void stop()
{
    if (!running)
    {
        return;
    }
    running = false;

    ui::Window& window = ui::Window::get();
    withdrawInstance(window.handle());
    removeTrayIcon();
    if (preventingSleep)
    {
        preventingSleep = false;
        SetThreadExecutionState(ES_CONTINUOUS);
    }
    window.cancel(blocklistTimer);
    App::get().removeRefreshListener(refreshListener);
    App::get().removeEventListener(eventListener);
}

bool handleMessage(UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_QUERYENDSESSION)
    {
        // Windows names this reason if saving outlasts its patience. Returning false lets the default
        // reply stand, which is yes.
        ShutdownBlockReasonCreate(ui::Window::get().handle(), L"Saving your transfers");
        return false;
    }
    if (message == WM_ENDSESSION)
    {
        if (wparam != 0)
        {
            endSession();
        }
        else
        {
            ShutdownBlockReasonDestroy(ui::Window::get().handle());
        }
        return true;
    }
    return handleTrayMessage(message, wparam, lparam);
}

void requestQuit()
{
    auto const& prefs = App::get().engine().prefs();
    Activity const activity = countActivity();
    if (prefs.checkQuit && (prefs.checkQuitDownloading ? activity.downloading : activity.active) > 0)
    {
        showQuitPrompt();
    }
    else
    {
        ui::Window::get().quit();
    }
}

void showDefaultPrompt()
{
    presentPrompt(std::make_unique<PromptPanel>(L"Open torrents with Nagare?", L"Windows asks you to choose this yourself, so this opens", L"Default Apps in Settings.", L"Not now", L"Open Settings", makeDefaultApp));
}

void showQuitPrompt()
{
    ui::Window& window = ui::Window::get();
    window.bringForward();
    int const active = countActivity().active;
    std::wstring const count = active == 1 ? L"There is an active transfer" : L"There are " + std::to_wstring(active) + L" active transfers";
    auto prompt = std::make_unique<PromptPanel>(L"Quit Nagare?", count + L" that will pause on quit.", L"They resume on the next launch.", L"Cancel", L"Quit", [] { ui::Window::get().quit(); });

    // The open panel closes through its own path, so the Add panel's bookkeeping stays right.
    if (auto* const open = dynamic_cast<ui::Panel*>(window.panel()); open != nullptr && dynamic_cast<PromptPanel*>(open) == nullptr)
    {
        open->close();
        window.after(ui::Panel::kAnimationMs + 30U, [shared = std::make_shared<std::unique_ptr<PromptPanel>>(std::move(prompt))] { presentPrompt(std::move(*shared)); });
        return;
    }
    presentPrompt(std::move(prompt));
}

} // namespace app::shell
