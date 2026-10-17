#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <dwmapi.h>
#include <algorithm>
#include <atomic>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

std::wstring Lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return std::towlower(c); });
    return s;
}
std::wstring ProcessPath(HANDLE process) {
    wchar_t buffer[32768];
    DWORD size = ARRAYSIZE(buffer);
    return QueryFullProcessImageNameW(process, 0, buffer, &size) ? std::wstring(buffer, size) : L"";
}
std::wstring ProcessPath(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return {};
    auto path = ProcessPath(h);
    CloseHandle(h);
    return path;
}
bool Protected(const std::wstring& path, DWORD pid) {
    const auto name = Lower(fs::path(path).filename().wstring());
    static const std::set<std::wstring> names{
        L"explorer.exe", L"sihost.exe", L"shellexperiencehost.exe", L"startmenuexperiencehost.exe",
        L"shellhost.exe", L"searchhost.exe", L"securityhealthsystray.exe",
        L"securityhealthservice.exe", L"applicationframehost.exe", L"taskhostw.exe", L"dwm.exe",
        L"winlogon.exe", L"csrss.exe", L"services.exe", L"lsass.exe", L"svchost.exe", L"smss.exe"
    };
    return path.empty() || pid <= 4 || pid == GetCurrentProcessId() || names.count(name);
}

// Private Explorer ABI. No icon preferences are modified. See README for compatibility.
struct NotifyItem {
    PWSTR exe;
    PWSTR tip;
    HICON icon;
    HWND window;
    DWORD preference;
    UINT id;
    GUID guid;
};
const IID CallbackId{0xd782ccba,0xafb0,0x43f1,{0x94,0xdb,0xfd,0xa3,0x77,0x9e,0xac,0xcb}};
const IID TrayId{0xd133ce13,0x3537,0x48ba,{0x93,0xa7,0xaf,0xcd,0x5d,0x20,0x53,0xb4}};
const CLSID TrayClass{0x25dead04,0x1eac,0x4911,{0x9e,0x3a,0xad,0x0a,0x4a,0x5b,0x60,0xfd}};
struct NotificationCallback : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Notify(ULONG event, NotifyItem* item) = 0;
};
struct TrayNotify : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE RegisterCallback(NotificationCallback*, ULONG*) = 0;
    virtual HRESULT STDMETHODCALLTYPE UnregisterCallback(ULONG*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPreference(const NotifyItem*) = 0;
    virtual HRESULT STDMETHODCALLTYPE EnableAutoTray(BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE DoAction(BOOL) = 0;
};
struct App {
    DWORD pid;
    std::wstring path, label;
};
class Collector final : public NotificationCallback {
    std::atomic<ULONG> refs{1};
public:
    std::map<DWORD, App> apps;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, CallbackId)) {
            *out = static_cast<NotificationCallback*>(this); AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        auto count = --refs;
        if (!count) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE Notify(ULONG, NotifyItem* item) override {
        if (!item || !IsWindow(item->window)) return S_OK;
        DWORD pid = 0;
        GetWindowThreadProcessId(item->window, &pid);
        auto path = ProcessPath(pid);
        if (Protected(path, pid)) return S_OK;
        // Cached tray records may retain HWNDs subsequently reused by other programs.
        if (!item->exe || Lower(fs::path(item->exe).filename().wstring()) !=
                          Lower(fs::path(path).filename().wstring())) return S_OK;
        std::wstring label = fs::path(path).stem().wstring();
        if (item->tip && *item->tip) label += L" — " + std::wstring(item->tip);
        std::replace(label.begin(), label.end(), L'\n', L' ');
        std::replace(label.begin(), label.end(), L'\r', L' ');
        apps.try_emplace(pid, App{pid, path, label});
        return S_OK;
    }
};

struct ScanResult { std::vector<App> apps; HRESULT result; };
std::wstring RegistryString(HKEY key, const wchar_t* name) {
    DWORD bytes = 0;
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, nullptr, &bytes) != ERROR_SUCCESS) return {};
    std::wstring text(bytes / sizeof(wchar_t), L'\0');
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, text.data(), &bytes) != ERROR_SUCCESS) return {};
    while (!text.empty() && text.back() == 0) text.pop_back();
    return text;
}
std::wstring ExpandIconPath(std::wstring path) {
    if (!path.empty() && path.front() == L'{') {
        auto end = path.find(L'}');
        if (end != std::wstring::npos) {
            GUID folder{};
            if (SUCCEEDED(CLSIDFromString(path.substr(0, end + 1).c_str(), &folder))) {
                PWSTR location = nullptr;
                if (SUCCEEDED(SHGetKnownFolderPath(folder, 0, nullptr, &location))) {
                    path = std::wstring(location) + path.substr(end + 1);
                    CoTaskMemFree(location);
                }
            }
        }
    }
    wchar_t expanded[32768];
    const DWORD n = ExpandEnvironmentStringsW(path.c_str(), expanded, ARRAYSIZE(expanded));
    return n && n <= ARRAYSIZE(expanded) ? std::wstring(expanded) : path;
}
using WindowMap = std::map<DWORD, std::vector<HWND>>;
struct ProcessEntry { DWORD parent; std::wstring name; };
using ProcessMap = std::map<DWORD, ProcessEntry>;
ProcessMap ProcessSnapshot() {
    ProcessMap processes;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return processes;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            processes.emplace(entry.th32ProcessID, ProcessEntry{entry.th32ParentProcessID, Lower(entry.szExeFile)});
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return processes;
}
DWORD UniqueAppRoot(const std::vector<DWORD>& candidates, const ProcessMap& processes) {
    // Electron/CEF auxiliary processes share the executable and may own hidden
    // windows. A single process family identifies one application, not ambiguity.
    std::set<DWORD> roots;
    for (DWORD pid : candidates) {
        DWORD root = pid;
        std::set<DWORD> visited{pid};
        auto current = processes.find(pid);
        while (current != processes.end()) {
            DWORD parent = current->second.parent;
            if (!visited.insert(parent).second) break;
            if (std::find(candidates.begin(), candidates.end(), parent) != candidates.end()) root = parent;
            current = processes.find(parent);
        }
        roots.insert(root);
    }
    return roots.size() == 1 ? *roots.begin() : 0;
}
BOOL CALLBACK CollectWindow(HWND window, LPARAM param) {
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    (*reinterpret_cast<WindowMap*>(param))[pid].push_back(window);
    return TRUE;
}
ScanResult ScanModern() {
    // Windows 11 retains icon identifiers in NotifyIconSettings. A record alone is
    // NOT evidence of a live icon: validate it with Shell_NotifyIconGetRect.
    ScanResult result{{}, S_OK};
    HKEY root = nullptr;
    const LSTATUS opened = RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\NotifyIconSettings",
                                         0, KEY_READ, &root);
    if (opened != ERROR_SUCCESS) { result.result = HRESULT_FROM_WIN32(opened); return result; }
    WindowMap windows;
    EnumWindows(CollectWindow, reinterpret_cast<LPARAM>(&windows));
    HWND message = nullptr;
    while ((message = FindWindowExW(HWND_MESSAGE, message, nullptr, nullptr)))
        CollectWindow(message, reinterpret_cast<LPARAM>(&windows));
    const auto snapshot = ProcessSnapshot();
    std::map<std::wstring, std::vector<DWORD>> processes;
    std::map<std::wstring, std::vector<DWORD>> inaccessible;
    for (const auto& [pid, handles] : windows) {
        auto path = ProcessPath(pid);
        if (!Protected(path, pid)) processes[Lower(path)].push_back(pid);
        else if (path.empty()) {
            const auto entry = snapshot.find(pid);
            if (entry != snapshot.end()) inaccessible[entry->second.name].push_back(pid);
        }
    }
    std::map<DWORD, App> found;
    for (DWORD index = 0;; ++index) {
        wchar_t name[256]; DWORD length = ARRAYSIZE(name);
        const LSTATUS next = RegEnumKeyExW(root, index, name, &length, nullptr, nullptr, nullptr, nullptr);
        if (next == ERROR_NO_MORE_ITEMS) break;
        if (next != ERROR_SUCCESS) { result.result = HRESULT_FROM_WIN32(next); break; }
        HKEY key = nullptr;
        if (RegOpenKeyExW(root, name, 0, KEY_READ, &key) != ERROR_SUCCESS) continue;
        auto path = ExpandIconPath(RegistryString(key, L"ExecutablePath"));
        auto tip = RegistryString(key, L"InitialTooltip");
        auto guidText = RegistryString(key, L"IconGuid");
        DWORD uid = 0, bytes = sizeof(uid);
        const bool hasUid = RegGetValueW(key, nullptr, L"UID", RRF_RT_REG_DWORD, nullptr, &uid, &bytes) == ERROR_SUCCESS;
        RegCloseKey(key);
        if (path.empty()) continue;
        std::vector<DWORD> candidates;
        const auto matching = processes.find(Lower(path));
        if (matching != processes.end()) candidates = matching->second;
        const auto restricted = inaccessible.find(Lower(fs::path(path).filename().wstring()));
        if (restricted != inaccessible.end())
            for (DWORD pid : restricted->second)
                if (!Protected(path, pid)) candidates.push_back(pid);
        if (candidates.empty()) continue;
        NOTIFYICONIDENTIFIER icon{};
        icon.cbSize = sizeof(icon);
        RECT rect{};
        std::vector<DWORD> owners;
        if (!guidText.empty() && SUCCEEDED(CLSIDFromString(guidText.c_str(), &icon.guidItem))) {
            const DWORD rootPid = UniqueAppRoot(candidates, snapshot);
            if (rootPid && SUCCEEDED(Shell_NotifyIconGetRect(&icon, &rect))) owners.push_back(rootPid);
        } else if (hasUid) {
            icon.uID = uid;
            for (DWORD pid : candidates) {
                for (HWND window : windows[pid]) {
                    icon.hWnd = window;
                    if (SUCCEEDED(Shell_NotifyIconGetRect(&icon, &rect))) { owners.push_back(pid); break; }
                }
            }
        }
        for (DWORD owner : owners) {
            std::replace(tip.begin(), tip.end(), L'\n', L' ');
            std::replace(tip.begin(), tip.end(), L'\r', L' ');
            auto actualPath = ProcessPath(owner);
            const bool limited = actualPath.empty();
            if (limited) actualPath = path;
            auto label = fs::path(actualPath).stem().wstring();
            if (!tip.empty()) label += L" — " + tip;
            if (limited) label += L" (limited access: administrator rights required)";
            found.try_emplace(owner, App{owner, actualPath, label});
        }
    }
    RegCloseKey(root);
    for (const auto& [pid, app] : found) result.apps.push_back(app);
    return result;
}
ScanResult Scan() {
    ScanResult result{{}, E_FAIL};
    TrayNotify* tray = nullptr;
    result.result = CoCreateInstance(TrayClass, nullptr, CLSCTX_LOCAL_SERVER, TrayId,
                                    reinterpret_cast<void**>(&tray));
    if (FAILED(result.result)) {
        result = ScanModern();
        std::sort(result.apps.begin(), result.apps.end(), [](const App& a, const App& b) {
            return Lower(a.label) < Lower(b.label);
        });
        return result;
    }
    auto* collector = new Collector;
    ULONG cookie = 0;
    result.result = tray->RegisterCallback(collector, &cookie);
    if (SUCCEEDED(result.result)) {
        // Explorer supplies its current (including hidden) icons synchronously.
        const auto unregisterResult = tray->UnregisterCallback(&cookie);
        if (FAILED(unregisterResult)) result.result = unregisterResult;
        for (auto& [pid, app] : collector->apps) result.apps.push_back(app);
    }
    tray->Release();
    collector->Release();
    if (FAILED(result.result)) result = ScanModern();
    std::sort(result.apps.begin(), result.apps.end(), [](const App& a, const App& b) {
        return Lower(a.label) < Lower(b.label);
    });
    return result;
}

bool IsTaskbarWindow(HWND window) {
    if (!IsWindowVisible(window) || window == GetShellWindow()) return false;
    const auto style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    if (!(style & WS_EX_APPWINDOW) &&
        ((style & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) || GetWindow(window, GW_OWNER))) return false;
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
        return false;
    return true;
}
BOOL CALLBACK CollectTaskbarApp(HWND window, LPARAM param) {
    if (!IsTaskbarWindow(window)) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    auto path = ProcessPath(pid);
    if (Protected(path, pid)) return TRUE;
    wchar_t title[1024]{};
    GetWindowTextW(window, title, ARRAYSIZE(title));
    auto label = fs::path(path).stem().wstring();
    if (*title) label += L" — " + std::wstring(title);
    // One checkbox per process, even when it owns several taskbar windows.
    reinterpret_cast<std::map<DWORD, App>*>(param)->try_emplace(pid, App{pid, path, label});
    return TRUE;
}
ScanResult ScanTaskbar() {
    std::map<DWORD, App> found;
    ScanResult result{{}, S_OK};
    if (!EnumWindows(CollectTaskbarApp, reinterpret_cast<LPARAM>(&found))) {
        const DWORD error = GetLastError();
        result.result = error ? HRESULT_FROM_WIN32(error) : E_FAIL;
    }
    for (const auto& [pid, app] : found) result.apps.push_back(app);
    std::sort(result.apps.begin(), result.apps.end(), [](const App& a, const App& b) {
        return Lower(a.label) < Lower(b.label);
    });
    return result;
}

struct Preferences {
    fs::path file;
    std::set<std::wstring> excluded;
    Preferences() {
        PWSTR local = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) {
            file = fs::path(local) / L"CloseAllSystray" / L"excluded.txt";
            CoTaskMemFree(local);
        }
    }
    void load() {
        excluded.clear();
        std::ifstream input(file, std::ios::binary);
        std::string line;
        while (std::getline(input, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, line.data(),
                                            static_cast<int>(line.size()), nullptr, 0);
            if (n <= 0) continue;
            std::wstring value(n, L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, line.data(),
                                static_cast<int>(line.size()), value.data(), n);
            excluded.insert(Lower(value));
        }
    }
    bool save() const {
        if (file.empty()) return false;
        std::error_code ec;
        fs::create_directories(file.parent_path(), ec);
        if (ec) return false;
        fs::path temp = file;
        temp += L".tmp";
        std::ofstream output(temp, std::ios::binary | std::ios::trunc);
        for (const auto& value : excluded) {
            int n = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                       nullptr, 0, nullptr, nullptr);
            std::string bytes(n, '\0');
            WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                bytes.data(), n, nullptr, nullptr);
            output << bytes << '\n';
        }
        output.close();
        if (!output) return false;
        return MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }
};

constexpr int ListId = 101, CloseId = 102, RefreshId = 103, ForceId = 104, TabsId = 105;
enum class AppSource { Systray, Taskbar };
AppSource currentSource = AppSource::Systray;
HWND mainWindow, list, status, closeButton, refreshButton, forceButton, hint, tabs;
HFONT font;
HIMAGELIST appIcons = nullptr;
Preferences preferences;
std::vector<App> apps;
bool loading = false, closing = false;
struct Target { HANDLE process; DWORD pid; std::wstring path; };
std::vector<Target> pending;
int closedCount = 0, failedCount = 0;
bool forceClose = true;
ULONGLONG deadline;

void Status(const std::wstring& text) { SetWindowTextW(status, text.c_str()); }
bool SaveChoices() {
    for (size_t i = 0; i < apps.size(); ++i) {
        auto key = Lower(apps[i].path);
        if (ListView_GetCheckState(list, static_cast<int>(i))) preferences.excluded.erase(key);
        else preferences.excluded.insert(key);
    }
    return preferences.save();
}
void Refresh() {
    loading = true;
    ListView_DeleteAllItems(list);
    // The list owns copies of the icons; discard the previous images on refresh.
    if (appIcons) {
        ListView_SetImageList(list, nullptr, LVSIL_SMALL);
        ImageList_Destroy(appIcons);
    }
    const int iconWidth = GetSystemMetrics(SM_CXSMICON);
    const int iconHeight = GetSystemMetrics(SM_CYSMICON);
    appIcons = ImageList_Create(iconWidth, iconHeight, ILC_COLOR32 | ILC_MASK, 16, 16);
    ListView_SetImageList(list, appIcons, LVSIL_SMALL);
    auto scan = currentSource == AppSource::Systray ? Scan() : ScanTaskbar();
    apps = std::move(scan.apps);
    for (size_t i = 0; i < apps.size(); ++i) {
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_IMAGE;
        item.iItem = static_cast<int>(i);
        item.pszText = apps[i].label.data();
        SHFILEINFOW info{};
        SHGetFileInfoW(apps[i].path.c_str(), 0, &info, sizeof(info), SHGFI_ICON | SHGFI_SMALLICON);
        HICON icon = info.hIcon ? info.hIcon : LoadIconW(nullptr, IDI_APPLICATION);
        item.iImage = appIcons && icon ? ImageList_AddIcon(appIcons, icon) : -1;
        if (info.hIcon) DestroyIcon(info.hIcon);
        ListView_InsertItem(list, &item);
        auto pid = std::to_wstring(apps[i].pid);
        ListView_SetItemText(list, item.iItem, 1, pid.data());
        ListView_SetItemText(list, item.iItem, 2, apps[i].path.data());
        ListView_SetCheckState(list, item.iItem, !preferences.excluded.count(Lower(apps[i].path)));
    }
    loading = false;
    if (FAILED(scan.result)) {
        wchar_t code[32];
        swprintf_s(code, L"0x%08lX", static_cast<unsigned long>(scan.result));
        Status(std::wstring(currentSource == AppSource::Systray ? L"System tray" : L"Taskbar") +
               L" scan unavailable (" + std::wstring(code) + L").");
    } else {
        Status(std::to_wstring(apps.size()) + L" application(s) in the " +
               (currentSource == AppSource::Systray ? L"system tray" : L"taskbar") +
               L". Close all applies to this tab only.");
    }
    EnableWindow(closeButton, !apps.empty());
}
void SelectTab() {
    const auto source = TabCtrl_GetCurSel(tabs) == 1 ? AppSource::Taskbar : AppSource::Systray;
    if (closing || !SaveChoices()) {
        TabCtrl_SetCurSel(tabs, currentSource == AppSource::Taskbar ? 1 : 0);
        if (!closing) Status(L"Error: exclusions could not be saved.");
        return;
    }
    currentSource = source;
    Refresh();
}
BOOL CALLBACK RequestClose(HWND window, LPARAM param) {
    const auto* target = reinterpret_cast<Target*>(param);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid == target->pid) PostMessageW(window, WM_CLOSE, 0, 0);
    return TRUE;
}
void BeginClose() {
    if (closing) return;
    if (!SaveChoices()) {
        MessageBoxExW(mainWindow, L"Could not save exclusions. Check your permissions in LocalAppData.",
                      L"Unable to save", MB_OK | MB_ICONERROR, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US));
        return;
    }
    closedCount = failedCount = 0;
    forceClose = SendMessageW(forceButton, BM_GETCHECK, 0, 0) == BST_CHECKED;
    for (size_t i = 0; i < apps.size(); ++i) {
        if (!ListView_GetCheckState(list, static_cast<int>(i))) continue;
        const auto& app = apps[i];
        HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION |
                                     (forceClose ? PROCESS_TERMINATE : 0), FALSE, app.pid);
        if (!process && forceClose) // Still allow a normal close if termination is denied.
            process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, app.pid);
        if (!process) { ++failedCount; continue; }
        auto path = ProcessPath(process);
        if (Protected(path, app.pid) || Lower(path) != Lower(app.path)) {
            CloseHandle(process); ++failedCount; continue;
        }
        BOOL critical = FALSE;
        if (!IsProcessCritical(process, &critical) || critical) {
            CloseHandle(process); ++failedCount; continue;
        }
        pending.push_back(Target{process, app.pid, path});
    }
    if (pending.empty()) {
        Status(failedCount ? L"Cannot access the selected applications (insufficient permissions or processes no longer running)." :
                           L"No applications selected.");
        return;
    }
    closing = true;
    EnableWindow(closeButton, FALSE); EnableWindow(refreshButton, FALSE);
    EnableWindow(list, FALSE); EnableWindow(forceButton, FALSE);
    EnableWindow(tabs, FALSE);
    for (auto& target : pending) EnumWindows(RequestClose, reinterpret_cast<LPARAM>(&target));
    deadline = GetTickCount64() + 3000;
    SetTimer(mainWindow, 1, 100, nullptr);
    Status(L"Closing applications…");
}
void PollClose() {
    for (auto it = pending.begin(); it != pending.end();) {
        const DWORD wait = WaitForSingleObject(it->process, 0);
        bool done = false;
        if (wait == WAIT_OBJECT_0) { ++closedCount; done = true; }
        else if (wait == WAIT_FAILED) { ++failedCount; done = true; }
        else if (GetTickCount64() >= deadline) {
            if (forceClose && TerminateProcess(it->process, 0)) {
                // Keep polling until the process handle is actually signaled.
                if (GetTickCount64() >= deadline + 5000) { ++failedCount; done = true; }
            } else { ++failedCount; done = true; }
        }
        if (done) { CloseHandle(it->process); it = pending.erase(it); }
        else ++it;
    }
    if (pending.empty()) {
        KillTimer(mainWindow, 1);
        closing = false;
        EnableWindow(refreshButton, TRUE); EnableWindow(list, TRUE); EnableWindow(forceButton, TRUE);
        EnableWindow(tabs, TRUE);
        Refresh();
        Status(std::to_wstring(closedCount) + L" application(s) closed, " +
               std::to_wstring(failedCount) + L" not closed. Exclusions saved.");
    }
}
void Layout(HWND window) {
    RECT r; GetClientRect(window, &r);
    int w = r.right, h = r.bottom;
    MoveWindow(tabs, 16, 12, w - 32, 32, TRUE);
    MoveWindow(hint, 16, 52, w - 32, 42, TRUE);
    MoveWindow(list, 16, 100, w - 32, h - 220, TRUE);
    MoveWindow(forceButton, 16, h - 110, w - 32, 26, TRUE);
    MoveWindow(refreshButton, 16, h - 76, 130, 32, TRUE);
    MoveWindow(closeButton, w - 156, h - 76, 140, 32, TRUE);
    MoveWindow(status, 16, h - 34, w - 32, 26, TRUE);
    ListView_SetColumnWidth(list, 0, std::max(210, (w - 32) * 40 / 100));
    ListView_SetColumnWidth(list, 2, std::max(180, (w - 32) * 60 / 100 - 90));
}
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
    case WM_CREATE: {
        mainWindow = window;
        font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH, L"Segoe UI");
        auto control = [&](DWORD ex, const wchar_t* cls, const wchar_t* text, DWORD style, int id) {
            HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style,
                                     0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                     GetModuleHandleW(nullptr), nullptr);
            SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            return c;
        };
        tabs = control(0, WC_TABCONTROLW, L"", WS_TABSTOP | TCS_FOCUSONBUTTONDOWN, TabsId);
        const wchar_t* tabNames[]{L"System tray", L"Taskbar"};
        for (int i = 0; i < 2; ++i) {
            TCITEMW item{}; item.mask = TCIF_TEXT;
            item.pszText = const_cast<wchar_t*>(tabNames[i]);
            TabCtrl_InsertItem(tabs, i, &item);
        }
        hint = control(0, L"STATIC", L"Select the applications to close in this tab. Exclusions are saved and shared between both tabs.", 0, 0);
        list = control(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS, ListId);
        ListView_SetExtendedListViewStyle(list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        const wchar_t* columns[]{L"Application", L"PID", L"Executable"};
        for (int i = 0; i < 3; ++i) {
            LVCOLUMNW col{}; col.mask = LVCF_TEXT | LVCF_WIDTH;
            col.pszText = const_cast<wchar_t*>(columns[i]); col.cx = i == 1 ? 75 : 300;
            ListView_InsertColumn(list, i, &col);
        }
        forceButton = control(0, L"BUTTON", L"Force close after 3 seconds (unsaved data may be lost)",
                              WS_TABSTOP | BS_AUTOCHECKBOX, ForceId);
        SendMessageW(forceButton, BM_SETCHECK, BST_CHECKED, 0);
        refreshButton = control(0, L"BUTTON", L"Refresh", WS_TABSTOP, RefreshId);
        closeButton = control(0, L"BUTTON", L"Close all", WS_TABSTOP | BS_DEFPUSHBUTTON, CloseId);
        status = control(0, L"STATIC", L"", 0, 0);
        preferences.load();
        Refresh();
        return 0;
    }
    case WM_SIZE: Layout(window); return 0;
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize = {780, 420}; return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == CloseId) BeginClose();
        if (LOWORD(wp) == RefreshId && !closing) Refresh();
        return 0;
    case WM_NOTIFY: {
        auto* header = reinterpret_cast<NMHDR*>(lp);
        if (header->idFrom == TabsId && header->code == TCN_SELCHANGE) { SelectTab(); return 0; }
        if (header->idFrom == ListId && header->code == LVN_ITEMCHANGED && !loading) {
            auto* changed = reinterpret_cast<NMLISTVIEW*>(lp);
            if ((changed->uChanged & LVIF_STATE) &&
                ((changed->uOldState ^ changed->uNewState) & LVIS_STATEIMAGEMASK)) {
                // Exclusions belong to the executable, including multiple instances.
                if (changed->iItem >= 0 && static_cast<size_t>(changed->iItem) < apps.size()) {
                    const auto key = Lower(apps[changed->iItem].path);
                    const bool checked = ListView_GetCheckState(list, changed->iItem);
                    loading = true;
                    for (size_t i = 0; i < apps.size(); ++i)
                        if (Lower(apps[i].path) == key)
                            ListView_SetCheckState(list, static_cast<int>(i), checked);
                    loading = false;
                }
                if (!SaveChoices()) Status(L"Error: exclusions could not be saved.");
            }
        }
        return 0;
    }
    case WM_TIMER: PollClose(); return 0;
    case WM_CLOSE:
        if (closing) { Status(L"Please wait until the current close operation finishes…"); return 0; }
        if (!SaveChoices()) {
            if (MessageBoxExW(window, L"Exclusions could not be saved. Exit anyway?",
                              L"Unable to save", MB_YESNO | MB_ICONWARNING,
                              MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US)) != IDYES) return 0;
        }
        DestroyWindow(window); return 0;
    case WM_DESTROY:
        if (appIcons) {
            ListView_SetImageList(list, nullptr, LVSIL_SMALL);
            ImageList_Destroy(appIcons);
            appIcons = nullptr;
        }
        DeleteObject(font); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com)) return 1;
    // Read-only diagnostics for validating enumeration without closing any application.
    if (std::wstring(commandLine) == L"--scan") {
        auto result = Scan();
        std::ofstream out(L"scan.txt", std::ios::binary);
        out << "HRESULT=" << std::hex << static_cast<unsigned long>(result.result) << '\n';
        for (const auto& app : result.apps) {
            const auto u8 = fs::path(app.path).u8string();
            out << std::dec << app.pid << '\t' << u8 << '\n';
        }
        CoUninitialize(); return FAILED(result.result) ? 2 : 0;
    }
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\CloseAllSystray.SingleInstance");
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(L"CloseAllSystrayWindow", nullptr)) {
            ShowWindow(existing, SW_RESTORE); SetForegroundWindow(existing);
        }
        if (mutex) CloseHandle(mutex);
        CoUninitialize(); return 0;
    }
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSW wc{};
    wc.lpfnWndProc = WindowProc; wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"CloseAllSystrayWindow";
    RegisterClassW(&wc);
    HWND window = CreateWindowExW(0, wc.lpszClassName, L"CloseAllSystray", WS_OVERLAPPEDWINDOW,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 980, 600,
                                  nullptr, nullptr, instance, nullptr);
    if (!window) { CloseHandle(mutex); CoUninitialize(); return 1; }
    ShowWindow(window, show);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    ReleaseMutex(mutex); CloseHandle(mutex);
    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
