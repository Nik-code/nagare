#include <windows.h>

#include <shellapi.h>

#include <string>
#include <string_view>

#include "app/add_panel.h"
#include "app/app.h"
#include "app/details_test.h"
#include "app/main_view.h"
#include "app/settings_panel.h"
#include "app/shell_integration.h"
#include "app/widget_gallery.h"
#include "engine/engine.h"
#include "ui/theme.h"
#include "ui/view.h"
#include "ui/window.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    auto const hasFlag = [argc, argv](std::wstring_view flag)
    {
        for (int i = 1; i < argc; ++i)
        {
            if (flag == argv[i])
            {
                return true;
            }
        }
        return false;
    };

    auto const flagValue = [argc, argv](std::wstring_view flag)
    {
        for (int i = 1; i + 1 < argc; ++i)
        {
            if (flag == argv[i])
            {
                return std::wstring(argv[i + 1]);
            }
        }
        return std::wstring();
    };

    // These need no window, so they also run over plain SSH and from the installer.
    if (auto const folder = flagValue(L"--engine-test"); !folder.empty())
    {
        return engine::runEngineTest(folder);
    }
    if (auto const folder = flagValue(L"--shell-test"); !folder.empty())
    {
        return app::shell::runShellTest(folder);
    }
    if (hasFlag(L"--register"))
    {
        app::shell::registerApp();
        return 0;
    }
    if (hasFlag(L"--unregister"))
    {
        app::shell::unregisterApp();
        return 0;
    }

    bool const sideBySide = app::shell::runsSideBySide(argc, argv);
    auto const requests = app::shell::openRequests(argc, argv);
    if (!sideBySide && !app::shell::claimInstance(requests))
    {
        return 0;
    }

    ui::Window& window = ui::Window::get();
    if (!window.create(L"Nagare", 1180, 780))
    {
        return 1;
    }
    window.setStartHidden(hasFlag(L"--hidden"));

    bool const gallery = hasFlag(L"--widgets") || hasFlag(L"--widgets-test");
    if (hasFlag(L"--widgets-test"))
    {
        app::runWidgetTests();
    }
    else if (hasFlag(L"--widgets"))
    {
        app::GalleryOptions options;
        options.panel = hasFlag(L"--panel");
        options.menu = hasFlag(L"--menu");
        options.states = hasFlag(L"--states");
        app::showWidgetGallery(options);
    }
    else if (!app::App::get().start(flagValue(L"--config")))
    {
        return 2;
    }
    else if (hasFlag(L"--panels-test"))
    {
        app::runPanelsTest();
    }

    if (hasFlag(L"--light"))
    {
        ui::Theme::get().setMode(ui::ThemeMode::Light);
    }
    else if (hasFlag(L"--dark"))
    {
        ui::Theme::get().setMode(ui::ThemeMode::Dark);
    }

    if (hasFlag(L"--details-test"))
    {
        app::runDetailsTests();
    }

    if (!gallery)
    {
        // --config means a test profile, so that run stays away from the registry too.
        app::shell::Options options;
        options.tray = !sideBySide || hasFlag(L"--tray");
        options.system = !sideBySide && !hasFlag(L"--config");
        app::shell::start(options);

        app::SettingsHooks& hooks = app::settingsHooks();
        hooks.startsWithWindows = app::shell::startsWithWindows;
        hooks.setStartsWithWindows = app::shell::setStartsWithWindows;
        hooks.keepsRunningInTray = app::shell::keepsRunningInTray;
        hooks.setKeepsRunningInTray = app::shell::setKeepsRunningInTray;
        hooks.isDefaultApp = app::shell::isDefaultApp;
        hooks.makeDefaultApp = app::shell::makeDefaultApp;

        for (std::wstring const& request : requests)
        {
            app::App::get().open(request);
        }
    }

    // --open <screen> jumps straight to a panel, so each screen can be captured on its own.
    for (int i = 1; i + 1 < argc && !gallery; ++i)
    {
        if (std::wstring_view(argv[i]) == L"--open")
        {
            app::openScreen(argv[i + 1]);
        }
        else if (std::wstring_view(argv[i]) == L"--search")
        {
            app::openSearchPanelWith(argv[i + 1]);
        }
    }

    if (hasFlag(L"--settings-test"))
    {
        app::runSettingsTest();
    }
    if (hasFlag(L"--main-test"))
    {
        app::runMainViewTests();
    }

    // --shot <path> captures the running app and exits, which is how builds are checked over SSH.
    for (int i = 1; i + 1 < argc; ++i)
    {
        if (std::wstring_view(argv[i]) == L"--shot")
        {
            std::wstring path = argv[i + 1];
            // Live searches take longer than the default wait, so --shot-delay can stretch it.
            DWORD delayMs = 1200;
            for (int j = 1; j + 1 < argc; ++j)
            {
                if (std::wstring_view(argv[j]) == L"--shot-delay")
                {
                    delayMs = static_cast<DWORD>(_wtoi(argv[j + 1]));
                }
            }
            window.after(
                delayMs,
                [&window, path]
                {
                    window.saveNextFrame(path);
                    window.invalidateAll();
                    window.after(400, [&window] { window.quit(); });
                });
        }
    }
    LocalFree(argv);

    int const result = window.run();
    if (!gallery)
    {
        app::shell::stop();
        app::App::get().shutdown();
    }
    return result;
}
