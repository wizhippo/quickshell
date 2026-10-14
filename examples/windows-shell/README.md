# Windows desktop shell example

A single Quickshell process hosts a bar on each connected monitor, an app
launcher, audio and power panels, calendar, control center and volume OSD.
The layout is inspired by the [Omarchy quattro shell](https://github.com/omacom/omarchy/tree/quattro/shell).
This is an independent Windows example using the APIs available in this port.

From the repository directory:

```powershell
.\run-windows.ps1 -Example shell
```

Build first with `.\build-windows.ps1` if needed. Both scripts default to
`build-quickshell` inside this repository. If your build is elsewhere, add
`-BuildDirectory C:\path\to\build-quickshell`. A CMake cache cannot be reused
across compilers, so switching `-Compiler` needs a different `-BuildDirectory`.
The existing small example remains available with `-Example panel`. An explicit
`-Config` takes precedence over `-Example`.

Normal `-Example shell` launches ask whether to replace Explorer when it owns
the desktop or taskbar. Yes requests a planned shutdown through Windows Restart
Manager, avoiding crash-style termination and shell recovery. File Explorer
windows in the same process close too. No keeps Explorer alongside Quickshell,
and Cancel stops the launch. Separate Explorer processes are left alone.
On normal exit, after our windows and tray have been destroyed, a second prompt
offers to start Windows Explorer if its desktop is absent. No leaves it stopped.
If Explorer is already running again, it is not started twice. These choices
apply only to the current session and do not change Windows logon settings.

Use `-NoExplorerPrompts` to skip lifecycle prompts. `-QuitAfter` verification
runs skip them automatically. Direct executable launches can opt in with
`--manage-explorer`. Forced termination or a crash cannot display an exit
prompt. `.\src\windows\test\explorer-session.ps1` verifies refusal and cleanup
without automatically stopping or starting Explorer. The explicit
`-ReplaceThenRestore` test temporarily closes Explorer, checks panel
responsiveness and that Explorer stays stopped, then restores it on exit
(or on failure). Save file operations before using that test.

| Surface | Behavior |
| --- | --- |
| Apps | Searches Windows Start applications, including packaged apps. Enter launches the selected result; Up/Down change selection. Refresh rescans the application list. |
| Network | Shows the live connection the way Omarchy does: a hero row with the network name, a status line and a signal or Ethernet mark, then the adapter, MAC, link speed, IPv4 address, gateway, DNS and security. Read from `Quickshell.Networking`; **Refresh** re-reads, and Wi-Fi/network settings still open Windows' own interfaces. |
| Clock | Opens a local calendar with month navigation and a Today button. |
| Audio | Controls native master output volume and mute, shows the output device, and opens Windows sound settings or its application mixer. The bar shows a speaker icon that crosses its waves out when muted; scroll over it to adjust volume with an OSD. |
| Power | Shows native battery/AC, charging, battery saver and remaining time when available. Opens power settings, locks the session, or confirms restart/shutdown. Exit Quickshell closes the shell. |
| Control center | Opens the network panel, read-only notification history, and Windows Bluetooth, display/brightness, notification settings and Task View interfaces. Includes calendar, wallpaper and shell appearance settings. |
| Wallpaper & background | Chooses an image or solid color for our desktop, with Fill/Fit modes, a preview and saved preferences. |
| Tray | Displays application icons hosted by Quickshell. The bar shows up to four non-hidden icons; the Tray panel lists all icons with left/right-click actions. |

Press **Win+Space** to toggle application search from anywhere, on the monitor
containing the pointer. If Windows or another app owns that combination, the
shell registers **Ctrl+Alt+Space** instead. The launcher displays the actual
shortcut and any registration errors. Registrations survive QML reload and are
released on exit; no Windows keyboard or language settings are changed.
The native `Quickshell.Windows.LauncherShortcut` singleton exposes `available`,
`shortcutText`, `errorString` and `activated(screenName)`. The registration is
shared by overlapping QML engines during reload. Test native message routing
and reload with `.\src\windows\test\launcher-shortcut.ps1`.

The experimental **Tray** host runs without Explorer. It creates its own hidden
`Shell_TrayWnd` and `TrayNotifyWnd` receiver windows, accepts the shell-side
notification-icon protocol and broadcasts `TaskbarCreated` so applications can
register again. Applications that do not handle that message need a restart.
The `Quickshell.Windows.TrayHost` singleton owns the receiver and icon model for
the process lifetime, so QML reload keeps registered icons. It handles add,
modify, delete, version negotiation, tooltips, hidden state and legacy/version-4
click callbacks. Dead application windows are pruned. Icons are copied to PNG
data URLs for QML; application context menus remain owned by their applications.
Version-4 callbacks carry the click anchor in `wParam` as Microsoft documents,
and `src/windows/test/tray_host.cpp` pins that to the actual cursor position.
Applications that ignore it, or misread it under DPI scaling, still open their
menu wherever they like: a process menu watch moves a `#32768` menu back under
the pointer when it appears more than 40 pixels from the click, within two
seconds of a tray click, leaving deliberately offset menus alone.

It starts only when no other shell owns the tray. If Explorer or another host
is running, the Tray panel explains the conflict and offers Retry. If the
receiver window is destroyed anyway, the host notices its own destruction,
drops the icons that could no longer update and reports the loss, so Retry
rebuilds it instead of leaving a stale, unchanging set on the bar. The tray
receiver itself does not stop other shells; the launcher's confirmation handles
Explorer replacement. Windows' configured logon shell is unchanged. The receiver uses
an undocumented compatibility protocol; balloons, icon rectangle queries,
Windows' built-in system icons, AppBar hosting and elevated-application behavior
are not implemented. 32-bit producers and broader third-party app compatibility
still need testing. This is a tray-host milestone toward a replacement desktop,
not a complete implementation of Explorer's shell services.

With `BUILD_TESTING=ON`, `ctest --test-dir build-quickshell -R windows-tray-host`
tests real `Shell_NotifyIconW` registration, images, updates, hidden state,
version-4 click callbacks, malformed packets, deletion and receiver cleanup.
It skips when another shell owns the tray rather than interfering with it.

Panels are frameless popups, like Omarchy's shell: no Windows title bar, no
taskbar or Alt+Tab entry, and no drop shadow. They open centered on the monitor
whose bar was clicked, directly below the bar or directly above it when the bar
is at the bottom, and are sized to fit smaller screens. Because there is no
chrome to move or resize them, a panel closes on Close, Escape or by clicking
anywhere outside it, and it reopens on its monitor when its bar button is
clicked again. Quickshell does not expose window flags, so `ShellPanel.qml`
applies `Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint` to the
backing `QQuickWindow` once it exists, and dismisses on the activation change
that follows losing focus.

The bar uses 34 logical pixels. When Quickshell hosts the tray,
panels position directly against monitor edges and do not send outgoing AppBar
requests to our notification receiver. AppBar workspace reservation relies on
an external shell's AppBar service, which this tray host does not yet replace.
The default style uses [Omarchy's Gruvbox palette](https://github.com/omacom/omarchy/blob/quattro/themes/gruvbox/colors.toml),
with warm charcoal surfaces, cream text, teal accents, square controls and a
compact 34-pixel bar. Typography prefers JetBrains Mono, then Cascadia Mono or
Consolas when installed. `Theme.qml` centralizes colors and the font fallback.
Button tooltips use a separate popup window outside the bar: below a top bar,
above a bottom bar. They no longer cover the icon-only launcher button.
The bar's status buttons are icons, not labels: signal bars, a speaker, a battery
outline whose fill tracks the level, the control center grid and the tray
overflow chevron. Each keeps its old text in its tooltip, and the selected
underline is released as soon as its panel is no longer visible.
Top/bottom placement, clock seconds and accent are saved in
`%LOCALAPPDATA%\quickshell-windows-shell.ini` and survive restarts and reloads.
Edit the QML files to customize the shell; Quickshell watches them for reloads.

Wallpaper is drawn by `WallpaperWindow.qml` on each monitor while our tray host
owns the desktop session. These windows sit below applications and do not
reserve workspace. Image loading is asynchronous and sized for monitor DPI.
Choose image uses Qt Quick's non-native file picker, avoiding Explorer dialogs
and the Windows personalization app. Image, Fill/Fit mode and background color
are saved with the shell preferences. Windows' own wallpaper settings are not
modified, so Explorer keeps its prior wallpaper when restored. The local
Wallpaper & background panel works without Explorer or Windows Settings.

`shell.qml` owns shared state and monitor bars. `WindowsServices.qml` discovers
apps and launches native interfaces. `ShellPanel.qml` contains the panels, and
`ShellButton.qml` supplies common button styling. Qt Quick Controls use the Basic
style explicitly so the example also works with Windows' native default style.

Windows settings buttons dispatch `ms-settings:` activation through the native
`System` singleton on a worker thread. Duplicate requests are ignored while one
is pending. The Windows notification settings control opens Windows Settings.
The worker initializes its own COM apartment. When replacing Explorer,
Quickshell now starts its own experimental C++ Settings host and completes the
window's presentation. Wi-Fi and power-page activation passed with the host
embedded in Quickshell and no Explorer process; usable Settings was also
confirmed interactively. The host survives QML reload and stops before the
Explorer restoration prompt. No separate host program is required.

This currently supports only the Windows DLL revisions verified in
[WINDOWS.md](../../WINDOWS.md#windows-settings-activation). A Windows update can
disable the host with a warning until its private interfaces are re-verified;
keep Explorer running in that case. First use downloads matching Microsoft
debug symbols into Quickshell's cache, so startup can take longer and requires
network access unless that cache is already populated. Other UWP applications
and the touch keyboard are not covered by this Settings host.

**Notification history** in the Control center reads toasts retained by Windows
through `Quickshell.Windows.NotificationHistory`. It displays application names,
timestamps, summaries and bodies as plain text. It refreshes every ten seconds
and offers manual Refresh. **Allow notification access** explicitly requests
permission; startup does not prompt. Denied access and native errors are shown
in the panel. No sending, dismissal or notification actions are implemented.
Desktop reads, destruction during a read, and the actual history panel with
17 retained notifications are verified. Permission prompts and revocation
still need interactive verification. A
sandboxed launch reported `0x80070057`; native desktop reads succeeded.

Application discovery uses `DesktopEntries` and a native AppsFolder scanner
built in its own small CMake target, without a PowerShell startup process.
The native results matched all 227 `Get-StartApps` IDs on this machine.
App IDs are passed as separate Explorer arguments; search input is never
executed as code. Refresh updates the native model and preserves surviving
entry objects. Packaged-app changes may require Refresh. Refresh and startup
do not change system audio or power settings. Restricted systems can use
Open Apps folder when discovery fails. Launching applications and detecting
installation changes still need interactive verification.

Windows retains responsibility for Wi-Fi and Bluetooth pairing, brightness
and virtual desktops. The example opens those
native interfaces rather than displaying invented device or workspace state.

The Network panel is the one place the shell reports connection state, and it
only reports what Windows reports. It reads `Quickshell.Networking`, whose
Windows backend uses `GetAdaptersAddresses` for adapters, addressing and DNS,
`GetIpForwardTable2` for the default route (the adapter list leaves that empty
when a filter driver installed it, as VPN and container stacks do), and the
WLAN API for the association (SSID and signal strength).
It re-reads on a short interval plus on demand via **Refresh**, and never
changes network state. Scanning, joining, forgetting and passphrase entry need
the WLAN API with elevation, so those stay in Windows' own settings and the
panel only links to them.

Two fields are deliberately left at their documented unknown values, because
the Windows SDK does not declare the values Windows reports for them:
`WifiNetwork.security`, whose `DOT11_AUTH_ALGORITHM` enumerators predate WPA3,
and `WifiNetwork.band`, for which `DOT11_PHY_TYPE` has no per-band enumerators
past 802.11n. Both would otherwise be guesses, so the panel omits them rather
than inventing an authentication or a frequency. Wi-Fi scanning is likewise
unimplemented, because the SDK has no `wlan_intf_opcode_bss_list`.

It does not implement Omarchy's Linux authentication, session lock surface,
Hyprland workspaces, PipeWire mixer, MPRIS players, weather service,
agents, plugin installer, a general global shortcut API or IPC. This Windows port does not
yet provide the matching service APIs. The current application-launch and
Task View actions still invoke Explorer and need separate replacements for a
fully independent desktop. The tray itself does not depend on Explorer.

The smoke configuration exercises every panel, both bar edges, the OSD and
application discovery/refresh without launching applications or changing audio
or power. It restores the original bar position:

```powershell
# Supply the Qt DLL directory through PATH as in run-windows.ps1.
& C:\path\to\build-quickshell\quickshell.exe `
    -p .\src\windows\test\manual\shell-example.qml --quit-after 15000
```

Look for `SHELL EXAMPLE PASS` and no QML warnings/errors. Actual pairing,
application launching, locking and restart/shutdown require interactive checks.

Also verify the real entry point through the launch script, since dynamic
loading in the panel test can resolve QML imports differently:

```powershell
.\run-windows.ps1 -Example shell -BuildDirectory C:\path\to\build-quickshell -QuitAfter 4000
```

## Open windows

Open application windows appear as one icon per process after the Applications
button, with a count for multiple windows. Click to restore the last window;
click an active group to cycle through its windows. A single-window group
minimizes on a second click. Applications using multiple processes can have
multiple buttons. Right-click
to show the application's native window menu (restore, move, size, minimize,
maximize and close, where supported). Scroll the icon row when it fills up.
The row excludes the shell's own panels, desktop and hidden utility windows.

Win+W requests that the foreground application close normally, allowing save
prompts. Windows reserves this key for Widgets on some systems, so the shell
uses a narrowly scoped keyboard hook if hotkey registration is rejected. The
hook is removed on exit. If that also fails, Ctrl+Alt+W is the fallback; the Apps
panel shows the actual binding. Elevated apps may reject control from a shell
running without elevation. These controls do not require Explorer.
