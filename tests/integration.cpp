// Runs the real window procedure against disposable fixtures only.
#include "../src/main.cpp"
#include <iostream>
#include <stdexcept>

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct Fixture {
    HANDLE handle;
    DWORD pid;
    fs::path path;
};
std::vector<Fixture> fixtures;
void Pump() {
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
}
void WaitForClose() {
    auto until = GetTickCount64() + 12000;
    while (closing && GetTickCount64() < until) { Pump(); Sleep(25); }
    Require(!closing, "close operation timed out");
}
void Populate(const std::vector<App>& selected) {
    loading = true;
    ListView_DeleteAllItems(list);
    apps = selected;
    for (size_t i = 0; i < apps.size(); ++i) {
        LVITEMW item{}; item.mask = LVIF_TEXT; item.iItem = static_cast<int>(i);
        item.pszText = apps[i].label.data();
        ListView_InsertItem(list, &item);
        ListView_SetCheckState(list, item.iItem, TRUE);
    }
    loading = false;
}
void ChangeTab(HWND window, int index) {
    TabCtrl_SetCurSel(tabs, index);
    NMHDR notification{tabs, TabsId, TCN_SELCHANGE};
    SendMessageW(window, WM_NOTIFY, TabsId, reinterpret_cast<LPARAM>(&notification));
}
int wmain() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const fs::path directory = fs::current_path() / L"build" / L"integration";
    fs::create_directories(directory);
    int code = 0;
    try {
        const ProcessMap families{{10, {1, L"app.exe"}}, {11, {10, L"app.exe"}},
                                  {12, {11, L"app.exe"}}, {20, {1, L"app.exe"}}};
        Require(UniqueAppRoot({10, 11, 12}, families) == 10, "main process family resolution failed");
        Require(UniqueAppRoot({10, 20}, families) == 0, "independent instances incorrectly merged");
        Preferences p;
        p.file = directory / L"preferences-utf8.txt";
        p.excluded = {L"c:\\échantillon\\outil.exe", L"c:\\apps\\keep.exe"};
        Require(p.save(), "preferences save failed");
        Preferences loaded; loaded.file = p.file; loaded.load();
        Require(loaded.excluded == p.excluded, "UTF-8 preferences round trip failed");
        const wchar_t* modes[]{L"", L"--stubborn", L"--message", L"--guid", L"--no-icon",
                               L"--both", L"--minimized", L"--tool", L"--owned"};
        for (int i = 0; i < 9; ++i) {
            auto path = directory / (L"fixture" + std::to_wstring(i) + L".exe");
            fs::copy_file(directory / L"fixture.exe", path, fs::copy_options::overwrite_existing);
            std::wstring command = L"\"" + path.wstring() + L"\" " + modes[i];
            STARTUPINFOW startup{}; startup.cb = sizeof(startup);
            startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION info{};
            Require(CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
                                    nullptr, nullptr, &startup, &info), "fixture launch failed");
            CloseHandle(info.hThread);
            fixtures.push_back({info.hProcess, info.dwProcessId, path});
            WaitForInputIdle(info.hProcess, 5000);
        }
        Sleep(1000);
        auto scan = Scan();
        Require(SUCCEEDED(scan.result), "tray scan failed");
        std::vector<App> testApps;
        for (int i = 0; i < 5; ++i) {
            auto found = std::find_if(scan.apps.begin(), scan.apps.end(), [&](const App& a) { return a.pid == fixtures[i].pid; });
            if (i == 4) Require(found == scan.apps.end(), "non-tray process included");
            else {
                Require(found != scan.apps.end(), "live fixture tray icon missing");
                testApps.push_back(*found);
            }
        }
        std::cout << "PASS enumeration: normal, stubborn, message-only, GUID; no-icon excluded\n";
        auto modern = ScanModern();
        Require(SUCCEEDED(modern.result), "modern tray scan failed");
        Require(std::any_of(modern.apps.begin(), modern.apps.end(), [&](const App& app) {
            return app.pid == fixtures[3].pid;
        }), "multi-process GUID application missing in modern scan");
        const auto snapshot = ProcessSnapshot();
        bool helperFound = false;
        for (const auto& [pid, entry] : snapshot) {
            if (entry.parent != fixtures[3].pid || entry.name != L"fixture3.exe") continue;
            helperFound = true;
            Require(std::none_of(modern.apps.begin(), modern.apps.end(), [&](const App& app) {
                return app.pid == pid;
            }), "auxiliary process incorrectly listed as tray owner");
        }
        Require(helperFound, "multi-process fixture did not create its helper");
        std::cout << "PASS multi-process GUID app detected once at its main process; independent roots rejected\n";
        auto taskbarScan = ScanTaskbar();
        Require(SUCCEEDED(taskbarScan.result), "taskbar scan failed");
        std::vector<App> taskbarFixtures;
        for (int i = 0; i < 9; ++i) {
            const auto count = std::count_if(taskbarScan.apps.begin(), taskbarScan.apps.end(), [&](const App& app) {
                return app.pid == fixtures[i].pid;
            });
            Require(count == ((i == 5 || i == 6) ? 1 : 0), "taskbar visibility/deduplication failed");
            if (count) taskbarFixtures.push_back(*std::find_if(taskbarScan.apps.begin(), taskbarScan.apps.end(), [&](const App& app) {
                return app.pid == fixtures[i].pid;
            }));
        }
        std::cout << "PASS taskbar scan: visible/minimized included once; hidden, tool and owned windows excluded\n";
        preferences.file = directory / L"ui-exclusions.txt";
        fs::remove(preferences.file);
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES};
        InitCommonControlsEx(&controls);
        WNDCLASSW wc{}; wc.lpfnWndProc = WindowProc;
        wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"CloseAllIntegration";
        RegisterClassW(&wc);
        HWND window = CreateWindowW(wc.lpszClassName, L"Test", WS_OVERLAPPEDWINDOW, 0, 0, 980, 600,
                                     nullptr, nullptr, wc.hInstance, nullptr);
        Require(window != nullptr, "main window creation failed");
        for (int i = 0; i < ListView_GetItemCount(list); ++i)
            Require(ListView_GetCheckState(list, i), "new application not checked by default");
        Require(TabCtrl_GetItemCount(tabs) == 2, "missing application tabs");
        ChangeTab(window, 1);
        Require(currentSource == AppSource::Taskbar, "taskbar tab selection failed");
        for (size_t i = 0; i < apps.size(); ++i)
            if (apps[i].pid == fixtures[5].pid || apps[i].pid == fixtures[6].pid)
                Require(ListView_GetCheckState(list, static_cast<int>(i)), "taskbar app not initially checked");
        Populate(taskbarFixtures); // Never close any user applications during testing.
        ListView_SetCheckState(list, 0, FALSE);
        Refresh();
        for (size_t i = 0; i < apps.size(); ++i)
            if (apps[i].pid == fixtures[5].pid)
                Require(!ListView_GetCheckState(list, static_cast<int>(i)), "taskbar exclusion lost on refresh");
        ChangeTab(window, 0);
        Require(currentSource == AppSource::Systray, "systray tab selection failed");
        bool sharedExclusion = false;
        for (size_t i = 0; i < apps.size(); ++i)
            if (apps[i].pid == fixtures[5].pid) {
                sharedExclusion = !ListView_GetCheckState(list, static_cast<int>(i));
            }
        Require(sharedExclusion, "exclusion not shared between tabs");
        ChangeTab(window, 1);
        Populate(taskbarFixtures);
        ListView_SetCheckState(list, 0, FALSE);
        SendMessageW(forceButton, BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageW(window, WM_COMMAND, CloseId, 0);
        Require(!IsWindowEnabled(tabs), "tab switching not disabled during close");
        WaitForClose();
        Require(WaitForSingleObject(fixtures[6].handle, 0) == WAIT_OBJECT_0, "taskbar application not closed");
        Require(WaitForSingleObject(fixtures[5].handle, 0) == WAIT_TIMEOUT, "unchecked taskbar app closed");
        Require(WaitForSingleObject(fixtures[0].handle, 0) == WAIT_TIMEOUT, "systray-only app affected by taskbar close");
        ChangeTab(window, 0);
        std::cout << "PASS tab selection, shared persistent exclusions and close scoped to active tab\n";
        Populate(testApps);
        ListView_SetCheckState(list, 3, FALSE);
        Preferences reloaded; reloaded.file = preferences.file; reloaded.load();
        Require(reloaded.excluded.count(Lower(testApps[3].path)), "unchecked state not persisted immediately");
        Refresh();
        for (size_t i = 0; i < apps.size(); ++i)
            if (apps[i].pid == fixtures[3].pid)
                Require(!ListView_GetCheckState(list, static_cast<int>(i)), "unchecked state lost on refresh");
        std::cout << "PASS checkbox defaults, immediate persistence and refresh\n";
        Populate(testApps);
        ListView_SetCheckState(list, 3, FALSE);
        SendMessageW(forceButton, BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageW(window, WM_COMMAND, CloseId, 0);
        WaitForClose();
        Require(WaitForSingleObject(fixtures[0].handle, 0) == WAIT_OBJECT_0, "graceful close failed");
        Require(WaitForSingleObject(fixtures[1].handle, 0) == WAIT_TIMEOUT, "force-disabled fixture terminated");
        Require(WaitForSingleObject(fixtures[3].handle, 0) == WAIT_TIMEOUT, "excluded fixture terminated");
        std::cout << "PASS graceful close, disabled force and exclusion\n";
        Populate({testApps[1], testApps[2]});
        SendMessageW(forceButton, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(window, WM_COMMAND, CloseId, 0);
        WaitForClose();
        Require(WaitForSingleObject(fixtures[1].handle, 0) == WAIT_OBJECT_0, "forced close failed");
        Require(WaitForSingleObject(fixtures[2].handle, 0) == WAIT_OBJECT_0, "message-only forced close failed");
        Require(WaitForSingleObject(fixtures[3].handle, 0) == WAIT_TIMEOUT, "excluded fixture terminated");
        Require(WaitForSingleObject(fixtures[4].handle, 0) == WAIT_TIMEOUT, "non-tray fixture terminated");
        std::cout << "PASS force close and untouched excluded/non-tray processes\n";
        DestroyWindow(window);
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; code = 1; }
    for (auto& fixture : fixtures) {
        if (WaitForSingleObject(fixture.handle, 0) == WAIT_TIMEOUT) {
            TerminateProcess(fixture.handle, 0);
            WaitForSingleObject(fixture.handle, 3000);
        }
        CloseHandle(fixture.handle);
    }
    CoUninitialize();
    return code;
}
