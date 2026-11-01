#include <windows.h>

#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <algorithm>
#include <cstdint>

#include "app/shell_integration.h"
#include "app/shell_internal.h"

namespace app::shell
{

namespace
{

constexpr wchar_t kClasses[] = L"Software\\Classes\\";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRegisteredApplications[] = L"Software\\RegisteredApplications";
constexpr wchar_t kKeepInTrayValue[] = L"KeepInTray";
constexpr wchar_t kBlocklistValue[] = L"BlocklistUpdated";
constexpr wchar_t kStartHiddenFlag[] = L" --hidden";
// registeredAppUser picks Nagare's own page on recent Windows 11. Older builds ignore it and open
// the general Default Apps page.
constexpr wchar_t kDefaultAppsPage[] = L"ms-settings:defaultapps?registeredAppUser=Nagare";
constexpr DWORD kProgIdChars = 128;

bool systemChanges = false;
bool startsWithWindowsThisRun = false;
bool keepInTrayThisRun = false;
int64_t blocklistUpdatedThisRun = 0;

void writeString(std::wstring const& key, wchar_t const* name, std::wstring const& value)
{
    RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), name, REG_SZ, value.c_str(), static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

template<typename Number>
[[nodiscard]] Number readNumber(wchar_t const* name, DWORD type)
{
    Number value = 0;
    DWORD size = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER, liveIdentity().softwareKey.c_str(), name, type, nullptr, &value, &size);
    return value;
}

template<typename Number>
void writeNumber(wchar_t const* name, DWORD type, Number value)
{
    RegSetKeyValueW(HKEY_CURRENT_USER, liveIdentity().softwareKey.c_str(), name, type, &value, sizeof(value));
}

[[nodiscard]] std::wstring quoted(std::wstring const& path)
{
    return L"\"" + path + L"\"";
}

void writeClass(std::wstring const& name, std::wstring const& description, std::wstring const& executable)
{
    auto const key = classKey(name);
    writeString(key, nullptr, description);
    writeString(key + L"\\DefaultIcon", nullptr, quoted(executable) + L",0");
    writeString(key + L"\\shell\\open\\command", nullptr, quoted(executable) + L" \"%1\"");
}

[[nodiscard]] std::wstring currentProgId(ASSOCF flags, wchar_t const* subject)
{
    wchar_t progId[kProgIdChars]{};
    DWORD size = kProgIdChars;
    return SUCCEEDED(AssocQueryStringW(flags, ASSOCSTR_PROGID, subject, nullptr, progId, &size)) ? progId : L"";
}

/// Windows keeps the .torrent key for others too, so it only goes if Nagare was its last user.
void removeIfEmpty(std::wstring const& key)
{
    HKEY handle = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_READ, &handle) != ERROR_SUCCESS)
    {
        return;
    }
    DWORD subkeys = 1;
    DWORD values = 1;
    RegQueryInfoKeyW(handle, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, &values, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(handle);
    if (subkeys == 0 && values == 0)
    {
        RegDeleteKeyW(HKEY_CURRENT_USER, key.c_str());
    }
}

[[nodiscard]] std::wstring startCommand()
{
    return quoted(executablePath()) + kStartHiddenFlag;
}

} // namespace

Identity const& liveIdentity()
{
    static Identity const identity{ L"Nagare", L"Software\\Nagare", L"Nagare.Torrent", L"Nagare.Magnet", L".torrent" };
    return identity;
}

std::wstring executablePath()
{
    std::wstring path(MAX_PATH, L'\0');
    for (;;)
    {
        DWORD const length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length < path.size())
        {
            path.resize(length);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

std::wstring capabilitiesKey(Identity const& identity)
{
    return identity.softwareKey + L"\\Capabilities";
}

std::wstring classKey(std::wstring_view name)
{
    std::wstring key = kClasses;
    key += name;
    return key;
}

wchar_t const* runKey()
{
    return kRunKey;
}

wchar_t const* registeredApplicationsKey()
{
    return kRegisteredApplications;
}

std::wstring readRegistryString(std::wstring const& key, wchar_t const* name)
{
    DWORD size = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), name, RRF_RT_REG_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS)
    {
        return {};
    }
    std::wstring value(size / sizeof(wchar_t), L'\0');
    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), name, RRF_RT_REG_SZ, nullptr, value.data(), &size) != ERROR_SUCCESS)
    {
        return {};
    }
    value.resize(wcsnlen(value.c_str(), value.size()));
    return value;
}

bool registryKeyExists(std::wstring const& key)
{
    HKEY handle = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_READ, &handle) != ERROR_SUCCESS)
    {
        return false;
    }
    RegCloseKey(handle);
    return true;
}

bool writeRegistration(Identity const& identity, std::wstring const& executable)
{
    auto const capabilities = capabilitiesKey(identity);
    auto const command = quoted(executable) + L" \"%1\"";
    if (readRegistryString(classKey(identity.torrentClass) + L"\\shell\\open\\command", nullptr) == command
        && readRegistryString(kRegisteredApplications, identity.appName.c_str()) == capabilities)
    {
        return false;
    }

    writeClass(identity.torrentClass, L"Torrent file", executable);
    writeClass(identity.magnetClass, L"URL:Magnet link", executable);
    writeString(classKey(identity.magnetClass), L"URL Protocol", L"");

    // OpenWithProgids adds Nagare beside whatever already opens .torrent. The extension's default
    // value and the magnet scheme itself stay untouched, so nothing the user chose is overridden.
    writeString(classKey(identity.torrentExtension) + L"\\OpenWithProgids", identity.torrentClass.c_str(), L"");

    writeString(capabilities, L"ApplicationName", identity.appName);
    writeString(capabilities, L"ApplicationDescription", L"A torrent client");
    writeString(capabilities, L"ApplicationIcon", quoted(executable) + L",0");
    writeString(capabilities + L"\\FileAssociations", identity.torrentExtension.c_str(), identity.torrentClass);
    writeString(capabilities + L"\\URLAssociations", L"magnet", identity.magnetClass);
    writeString(kRegisteredApplications, identity.appName.c_str(), capabilities);
    return true;
}

void removeRegistration(Identity const& identity)
{
    auto const extension = classKey(identity.torrentExtension);
    RegDeleteKeyValueW(HKEY_CURRENT_USER, kRegisteredApplications, identity.appName.c_str());
    RegDeleteKeyValueW(HKEY_CURRENT_USER, (extension + L"\\OpenWithProgids").c_str(), identity.torrentClass.c_str());
    removeIfEmpty(extension + L"\\OpenWithProgids");
    removeIfEmpty(extension);
    RegDeleteTreeW(HKEY_CURRENT_USER, classKey(identity.torrentClass).c_str());
    RegDeleteTreeW(HKEY_CURRENT_USER, classKey(identity.magnetClass).c_str());
    RegDeleteTreeW(HKEY_CURRENT_USER, identity.softwareKey.c_str());
}

void setRunCommand(std::wstring const& valueName, std::wstring const& command)
{
    if (command.empty())
    {
        RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, valueName.c_str());
    }
    else
    {
        writeString(kRunKey, valueName.c_str(), command);
    }
}

std::wstring runCommand(std::wstring const& valueName)
{
    return readRegistryString(kRunKey, valueName.c_str());
}

void allowSystemChanges(bool allowed)
{
    systemChanges = allowed;
}

int64_t blocklistUpdated()
{
    return systemChanges ? readNumber<int64_t>(kBlocklistValue, RRF_RT_REG_QWORD) : blocklistUpdatedThisRun;
}

void setBlocklistUpdated(int64_t unixTime)
{
    blocklistUpdatedThisRun = unixTime;
    if (systemChanges)
    {
        writeNumber(kBlocklistValue, REG_QWORD, unixTime);
    }
}

void registerApp()
{
    auto const executable = executablePath();
    if (writeRegistration(liveIdentity(), executable))
    {
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    }
    // A portable copy may have moved since Start with Windows was turned on.
    auto const& name = liveIdentity().appName;
    if (auto const current = runCommand(name); !current.empty() && current != startCommand())
    {
        setRunCommand(name, startCommand());
    }
}

void unregisterApp()
{
    closeRunningInstance();
    setRunCommand(liveIdentity().appName, {});
    removeRegistration(liveIdentity());
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

bool startsWithWindows()
{
    return systemChanges ? !runCommand(liveIdentity().appName).empty() : startsWithWindowsThisRun;
}

void setStartsWithWindows(bool on)
{
    startsWithWindowsThisRun = on;
    if (systemChanges)
    {
        setRunCommand(liveIdentity().appName, on ? startCommand() : std::wstring());
    }
}

bool keepsRunningInTray()
{
    return systemChanges ? readNumber<DWORD>(kKeepInTrayValue, RRF_RT_REG_DWORD) != 0 : keepInTrayThisRun;
}

void setKeepsRunningInTray(bool on)
{
    keepInTrayThisRun = on;
    if (systemChanges)
    {
        writeNumber<DWORD>(kKeepInTrayValue, REG_DWORD, on ? 1U : 0U);
    }
}

bool isDefaultApp()
{
    auto const& identity = liveIdentity();
    return currentProgId(ASSOCF_NONE, identity.torrentExtension.c_str()) == identity.torrentClass && currentProgId(ASSOCF_IS_PROTOCOL, L"magnet") == identity.magnetClass;
}

void makeDefaultApp()
{
    if (systemChanges)
    {
        registerApp();
        ShellExecuteW(nullptr, L"open", kDefaultAppsPage, nullptr, nullptr, SW_SHOWNORMAL);
    }
}

} // namespace app::shell
