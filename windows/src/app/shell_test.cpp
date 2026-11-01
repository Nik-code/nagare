#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include "app/shell_integration.h"
#include "app/shell_internal.h"
#include "ui/theme.h"

// nagare.exe --shell-test <folder> runs without a window, so it works over plain SSH. It registers
// under its own test names and removes them again, leaving what the real app would write alone.
// Each check writes one line to <folder>\shell-test.log, and any failure makes the exit code non-zero.

namespace app::shell
{

namespace
{

class Log
{
public:
    explicit Log(std::wstring const& path)
    {
        _wfopen_s(&file_, path.c_str(), L"w");
    }
    Log(Log const&) = delete;
    Log& operator=(Log const&) = delete;
    ~Log()
    {
        if (file_ != nullptr)
        {
            fclose(file_);
        }
    }

    void check(bool passed, char const* name)
    {
        failures_ += passed ? 0 : 1;
        if (file_ != nullptr)
        {
            fprintf(file_, "%s %s\n", passed ? "ok  " : "FAIL", name);
            fflush(file_);
        }
    }

    [[nodiscard]] int failures() const { return failures_; }

private:
    FILE* file_ = nullptr;
    int failures_ = 0;
};

void checkAssets(Log& log)
{
    log.check(!ui::loadAsset(L"fonts\\Outfit-Medium.ttf").empty(), "the font is embedded");
    log.check(!ui::loadAsset(L"art\\ArtSky-dark.jpg").empty(), "art is embedded");
    log.check(!ui::loadAsset(L"art\\ArtRowDownload2-light.jpg").empty(), "row art is embedded");
    log.check(!ui::loadAsset(L"icons\\ChevronDown.svg").empty(), "icons are embedded");
    log.check(ui::loadAsset(L"icons\\NoSuchIcon.svg").empty(), "a missing asset comes back empty");
}

void checkCommandLines(Log& log)
{
    std::vector<std::wstring> storage{ L"nagare.exe", L"--demo", L"--open", L"add", L"--config", L"C:\\config", L"relative.torrent", L"MAGNET:?xt=urn:btih:abc", L"C:\\a b\\c.torrent" };
    std::vector<wchar_t*> arguments;
    for (auto& argument : storage)
    {
        arguments.push_back(argument.data());
    }
    auto const requests = openRequests(static_cast<int>(arguments.size()), arguments.data());
    log.check(requests.size() == 3, "flags and their values are not requests");
    log.check(requests.size() == 3 && requests[0].size() > 3 && requests[0][1] == L':' && requests[0].ends_with(L"\\relative.torrent"), "a relative path becomes absolute");
    log.check(requests.size() == 3 && requests[1] == storage[7], "a magnet link passes through whatever its case");
    log.check(requests.size() == 3 && requests[2] == storage[8], "an absolute path passes through");

    log.check(runsSideBySide(static_cast<int>(arguments.size()), arguments.data()), "--demo runs side by side");
    for (wchar_t const* flag : { L"--demo-light", L"--shot", L"--widgets", L"--widgets-test", L"--engine-test", L"--shell-test" })
    {
        std::wstring copy = flag;
        wchar_t* probe[] = { storage[0].data(), copy.data() };
        log.check(runsSideBySide(2, probe), "a build flag runs side by side");
    }
    std::wstring plain = L"C:\\a.torrent";
    wchar_t* probe[] = { storage[0].data(), plain.data() };
    log.check(!runsSideBySide(2, probe), "a torrent path takes the instance lock");
}

void checkRegistration(Log& log)
{
    Identity const test{ L"NagareShellTest", L"Software\\NagareShellTest", L"NagareShellTest.Torrent", L"NagareShellTest.Magnet", L".nagareshelltest" };
    std::wstring const executable = L"C:\\Nagare Test\\nagare.exe";
    std::wstring const command = L"\"C:\\Nagare Test\\nagare.exe\" \"%1\"";
    auto const capabilities = capabilitiesKey(test);

    removeRegistration(test);
    log.check(!registryKeyExists(classKey(test.torrentClass)), "the test names start out clean");
    bool const realBefore = registryKeyExists(classKey(liveIdentity().torrentClass));

    log.check(writeRegistration(test, executable), "the first registration writes");
    log.check(readRegistryString(classKey(test.torrentClass) + L"\\shell\\open\\command", nullptr) == command, "the torrent class opens the app");
    log.check(readRegistryString(classKey(test.magnetClass) + L"\\shell\\open\\command", nullptr) == command, "the magnet class opens the app");
    log.check(readRegistryString(classKey(test.torrentClass) + L"\\DefaultIcon", nullptr) == L"\"C:\\Nagare Test\\nagare.exe\",0", "the torrent class has the icon");
    log.check(registryKeyExists(classKey(test.magnetClass)) && !readRegistryString(classKey(test.magnetClass), nullptr).empty(), "the magnet class is described");
    log.check(readRegistryString(capabilities + L"\\FileAssociations", test.torrentExtension.c_str()) == test.torrentClass, "capabilities claim the extension");
    log.check(readRegistryString(capabilities + L"\\URLAssociations", L"magnet") == test.magnetClass, "capabilities claim magnet");
    log.check(readRegistryString(capabilities, L"ApplicationName") == test.appName, "capabilities name the app");
    log.check(readRegistryString(registeredApplicationsKey(), test.appName.c_str()) == capabilities, "RegisteredApplications points at the capabilities");
    log.check(readRegistryString(classKey(test.torrentExtension), nullptr).empty(), "the extension's default is left alone");
    log.check(!writeRegistration(test, executable), "registering again changes nothing");
    RegSetKeyValueW(HKEY_CURRENT_USER, (classKey(test.torrentExtension) + L"\\OpenWithProgids").c_str(), L"SomeOtherApp.Torrent", REG_SZ, L"", sizeof(wchar_t));
    log.check(writeRegistration(test, L"C:\\Moved\\nagare.exe"), "a moved executable registers again");
    log.check(readRegistryString(classKey(test.torrentClass) + L"\\shell\\open\\command", nullptr) == L"\"C:\\Moved\\nagare.exe\" \"%1\"", "the new path lands");

    removeRegistration(test);
    log.check(registryKeyExists(classKey(test.torrentExtension) + L"\\OpenWithProgids"), "another app's Open With entry survives");
    RegDeleteKeyValueW(HKEY_CURRENT_USER, (classKey(test.torrentExtension) + L"\\OpenWithProgids").c_str(), L"SomeOtherApp.Torrent");
    removeRegistration(test);
    log.check(!registryKeyExists(classKey(test.torrentClass)), "unregistering removes the torrent class");
    log.check(!registryKeyExists(classKey(test.magnetClass)), "unregistering removes the magnet class");
    log.check(!registryKeyExists(test.softwareKey), "unregistering removes the capabilities");
    log.check(readRegistryString(registeredApplicationsKey(), test.appName.c_str()).empty(), "unregistering removes the RegisteredApplications value");
    log.check(!registryKeyExists(classKey(test.torrentExtension)), "unregistering removes the empty extension key");
    log.check(registryKeyExists(classKey(liveIdentity().torrentClass)) == realBefore, "the real registration was not touched");
    log.check(realBefore || !isDefaultApp(), "an app that isn't registered is not the default");

    setRunCommand(test.appName, L"\"C:\\Nagare Test\\nagare.exe\" --hidden");
    log.check(runCommand(test.appName) == L"\"C:\\Nagare Test\\nagare.exe\" --hidden", "the Run value lands");
    setRunCommand(test.appName, {});
    log.check(runCommand(test.appName).empty(), "the Run value goes");
}

void checkDryRun(Log& log)
{
    // Off is the default, so these flips must stay in memory.
    auto const before = runCommand(liveIdentity().appName);
    bool const keyBefore = registryKeyExists(liveIdentity().softwareKey);
    setStartsWithWindows(before.empty());
    log.check(runCommand(liveIdentity().appName) == before, "the hook leaves the Run key alone in a dry run");
    log.check(startsWithWindows() == before.empty(), "the Run hook remembers within the run");
    setKeepsRunningInTray(true);
    log.check(keepsRunningInTray(), "the tray hook remembers within the run");
    log.check(registryKeyExists(liveIdentity().softwareKey) == keyBefore, "the tray hook stays out of the registry");
}

} // namespace

int runShellTest(std::wstring const& folder)
{
    Log log(folder + L"\\shell-test.log");
    checkAssets(log);
    checkCommandLines(log);
    checkRegistration(log);
    checkDryRun(log);
    return log.failures() == 0 ? 0 : 1;
}

} // namespace app::shell
