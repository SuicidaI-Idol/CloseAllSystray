# CloseAllSystray

A native Windows C++17 application for closing applications in the system tray (including hidden icons) and applications open in the taskbar. The application and installer use English. Window titles and tooltips supplied by other applications retain their original language.

## Usage

Open this folder in **Visual Studio Code** and press **F5** to build Debug and launch under GDB. Breakpoints work in `src/main.cpp`.

The configuration uses the tools installed on this machine:

- GCC: `C:/msys64/mingw64/bin/g++.exe`
- GDB: `C:/msys64/mingw64/bin/gdb.exe`
- VS Code extension: `ms-vscode.cpptools`.

New applications are checked by default. Uncheck applications to keep, then click **Close all**. Exclusions are saved immediately, even if you exit without clicking Close all. **Refresh** detects newly opened applications.

The **System tray** and **Taskbar** tabs show separate lists. Taskbar lists applications with visible or minimized windows, their icons and a window title. Each process appears once even if it owns multiple windows. Hidden windows, utility windows and pinned applications without an open window are omitted. Protected system processes, including Explorer, remain excluded. Detection follows the [Windows taskbar window rules](https://learn.microsoft.com/en-us/windows/win32/shell/taskbar); custom taskbar buttons and windows on other virtual desktops may differ.

**Close all affects only the checked applications in the active tab.** Exclusions are shared between tabs by executable path and survive tab changes and restarts. Closing targets the application process, so all its windows may close. Tab switching is disabled during a close operation.

Closing first sends `WM_CLOSE` to the process windows. By default, processes still running after three seconds are terminated: **unsaved data may be lost**. Uncheck **Force close after 3 seconds** to let applications refuse closing or display a save dialog. The status reports how many processes closed and how many did not.

Exclusions are stored as UTF-8 in `%LOCALAPPDATA%\CloseAllSystray\excluded.txt`, keyed by full executable path. Instances of the same executable share an exclusion. Delete this file to reset all checkboxes. Only one instance of CloseAllSystray can run at a time.

## Building

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Configuration Release
```

Output: `build/Debug/CloseAllSystray.exe` and `build/Release/CloseAllSystray.exe`. The GCC runtime is statically linked; no MSYS2 DLLs need to be distributed.

`CMakeLists.txt` also supports GCC/MinGW and Visual Studio with C++ tools installed. CMake is not needed for F5 debugging.

## Installer

In VS Code, choose **Terminal → Run Task → Build Release + Installer**. This builds Release and generates `dist/CloseAllSystray-Setup.exe` using Inno Setup 6, already installed on this machine.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-installer.ps1
# Set a version:
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-installer.ps1 -Version 1.0.1
```

On another development machine, install Inno Setup if needed: `winget install --id JRSoftware.InnoSetup -e -s winget` ([official downloads](https://jrsoftware.org/isdl.php)).

The English-language installer installs for all users in `%ProgramFiles%\CloseAllSystray` and requires administrator elevation. It adds a Start menu shortcut, offers an optional desktop shortcut and supports uninstalling through Windows Settings. Personal exclusions are retained when uninstalling. Close the application before installing or updating.

Application executables and build files are ignored by Git. Only `dist/CloseAllSystray-Setup.exe` is exempt and can be added to the repository.

## Detection and limitations

Windows has no public API for enumerating all notification icons. The application first tries Explorer's private `ITrayNotify` interface. On newer Windows versions where it is unavailable, it reads identifiers from `HKCU\Control Panel\NotifyIconSettings`, resolves known-folder paths, and validates each icon using `Shell_NotifyIconGetRect` and an active process. Stale registry entries alone are never listed. The tray registry is read only.

The fallback has been verified on Windows build 26300 on this machine. Windows updates may require detection changes. Icons without usable registry identifiers may be missing. For GUID icons, auxiliary processes sharing the same executable are grouped with their main process, allowing applications such as Discord and ChatGPT to be detected. Associations with multiple independent process families are skipped.

Icons whose processes deny access are also shown when Windows validates their window handle and UID. They are marked **limited access: administrator rights required**, for example the NVIDIA container owning the NVIDIA settings icon. Running CloseAllSystray as administrator may be necessary to close them. Listing a process does not bypass identity validation before closing it.

Explorer, CloseAllSystray and several system processes are excluded; critical processes are also refused before closing. Elevated applications can deny access from a standard session. Closing an application does not necessarily stop its services, drivers or all child processes: the icon owner is targeted, or the main process of its family for GUID icons. A service or launcher may restart an application.

References: [Microsoft notification area API](https://learn.microsoft.com/en-us/windows/win32/shell/notification-area), [Shell_NotifyIconGetRect](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shell_notifyicongetrect), [private interface declarations in Chromium](https://chromium.googlesource.com/chromium/src/+/a4993e2d04893594d4e5b3f4fa5ed1c51d13f824/chrome/browser/ui/views/status_icons/status_tray_state_changer_win.cc).

## Verification

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test.ps1
```

Tests create disposable fixture executables only. They cover UID and GUID detection, multi-process applications, hidden and message-only windows, exclusion of non-tray processes, UTF-8 persistence, default checkbox states, refresh, graceful and forced closing, and preservation of unchecked processes. Taskbar tests cover visible and minimized windows, deduplication, hidden/tool/owned window filtering, tab switching, shared exclusions and closing scoped to the active tab. No user applications are closed during tests. Test artifacts and preferences are stored in `build/integration/`.

`CloseAllSystray.exe --scan` writes a local `scan.txt` diagnostic without closing applications. It contains detected process IDs and executable paths and is ignored by Git.

