#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#include <windows.h>
#include <shellapi.h>
#include <string>
NOTIFYICONDATAW icon{};
bool stubborn = false;
HANDLE helperJob = nullptr;
LRESULT CALLBACK FixtureProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_CLOSE && stubborn) return 0;
    if (message == WM_DESTROY) {
        Shell_NotifyIconW(NIM_DELETE, &icon);
        if (helperJob) { CloseHandle(helperJob); helperJob = nullptr; }
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR args, int) {
    std::wstring mode(args);
    stubborn = mode == L"--stubborn";
    WNDCLASSW wc{};
    wc.lpfnWndProc = FixtureProc; wc.hInstance = instance; wc.lpszClassName = L"CloseAllTestFixture";
    RegisterClassW(&wc);
    const bool taskbar = mode == L"--both" || mode == L"--minimized";
    const bool tool = mode == L"--tool";
    const bool owned = mode == L"--owned";
    HWND owner = owned ? CreateWindowW(wc.lpszClassName, L"Hidden owner", WS_OVERLAPPED,
                                       0, 0, 100, 100, nullptr, nullptr, instance, nullptr) : nullptr;
    HWND window = CreateWindowExW(tool ? WS_EX_TOOLWINDOW : 0, wc.lpszClassName, L"CloseAll test fixture", WS_OVERLAPPEDWINDOW,
                                 0, 0, 100, 100, mode == L"--message" ? HWND_MESSAGE : nullptr,
                                 nullptr, instance, nullptr);
    if (owned) SetWindowLongPtrW(window, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
    if (taskbar || tool || owned) ShowWindow(window, mode == L"--minimized" ? SW_SHOWMINNOACTIVE : SW_SHOWNOACTIVATE);
    if (mode == L"--both") {
        HWND second = CreateWindowW(wc.lpszClassName, L"Second taskbar window", WS_OVERLAPPEDWINDOW,
                                     110, 0, 100, 100, nullptr, nullptr, instance, nullptr);
        ShowWindow(second, SW_SHOWMINNOACTIVE);
    }
    icon.cbSize = sizeof(icon); icon.hWnd = window; icon.uID = 12345;
    icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    icon.uCallbackMessage = WM_APP + 1;
    icon.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    lstrcpyW(icon.szTip, L"CloseAll integration fixture");
    if (mode == L"--guid") {
        icon.uFlags |= NIF_GUID;
        icon.guidItem = {0xc2ec159b,0xc43d,0x4ab9,{0x88,0xd7,0x72,0x27,0xa1,0xa2,0x96,0x06}};
    }
    if (mode != L"--no-icon" && mode != L"--minimized" && mode != L"--tool" && mode != L"--owned" &&
        !Shell_NotifyIconW(NIM_ADD, &icon)) return 2;
    if (mode == L"--guid") {
        // Reproduce Electron/CEF: same executable, auxiliary process with its own
        // hidden window, but only the main process registers a tray icon.
        helperJob = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!helperJob || !SetInformationJobObject(helperJob, JobObjectExtendedLimitInformation,
                                                   &limits, sizeof(limits))) return 3;
        wchar_t executable[32768];
        GetModuleFileNameW(nullptr, executable, ARRAYSIZE(executable));
        std::wstring command = L"\"" + std::wstring(executable) + L"\" --no-icon";
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION child{};
        if (!CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED,
                            nullptr, nullptr, &startup, &child)) return 4;
        if (!AssignProcessToJobObject(helperJob, child.hProcess)) {
            TerminateProcess(child.hProcess, 1);
            CloseHandle(child.hThread); CloseHandle(child.hProcess); return 5;
        }
        ResumeThread(child.hThread);
        WaitForInputIdle(child.hProcess, 5000);
        CloseHandle(child.hThread); CloseHandle(child.hProcess);
    }
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return 0;
}
