# Experimental Windows port

This port starts from upstream Quickshell 0.3.2, commit
`4f508be500dea6e5732cc3d50382a0048b17e7b1`. It uses the actual Quickshell
core, QML modules, configuration scanner, engine generations and reloadable
window proxies. `PanelWindow` is supplied by a new Windows platform overlay.

## Build and run

Verified toolchain: Windows 11, Qt 6.11.2 MSVC 2022 64-bit and Visual Studio
Build Tools 2022 (Windows SDK 10.0.26100) from `C:\Qt`. From the repository
directory:

```powershell
.\build-windows.ps1
.\run-windows.ps1 -Example shell
```

MSVC is the only supported toolchain. MinGW was tried first and abandoned
because its SDK omits declarations the Windows backends need, including
`wlan_intf_opcode_bss_list`, `WLAN_NOTIFICATION_SOURCE_ONE`, `NotifyAddrChange`
and `NET_IFCHANGE_*`, and because its `DOT11_AUTH_ALGORITHM` enumerators predate
WPA3 and so disagree with what Windows reports at runtime. The Windows SDK
shipped with MSVC is used instead.

Both scripts accept `-QtRoot`, `-QtVersion` and `-BuildDirectory` when the
installation differs. The build script also accepts
`-BuildType` and `-Tests` (equivalent to `-DBUILD_TESTING=ON`). It locates the
Visual Studio installation through `vswhere` and imports the `vcvars64`
environment itself, so no developer prompt is needed.

Both scripts default to `build-quickshell` inside this repository, so a plain
`.\build-windows.ps1` followed by `.\run-windows.ps1 -Example shell` works with
no arguments. The run script supplies the matching Qt DLL directory through
`PATH`.

For a Release build, use `.\build-windows.ps1 -BuildType Release` and then
`.\run-windows.ps1 -Example shell`. Successful builds stage project notices
in `<build>/legal`. Before distributing binaries, follow
[the Windows release guide](WINDOWS-RELEASE.md) for matching source, dependency
notices and recipient rights. The build script does not package a Qt runtime
or create a complete redistributable release.

To use another QML file:

```powershell
.\run-windows.ps1 -Config C:\path\to\shell.qml
```

The Windows launcher accepts `-p/--path` (file or directory), a positional
configuration path, `--help`, `--version`, and `--quit-after <milliseconds>`.
It defaults to `shell.qml` in the current directory. It deliberately does not
implement the upstream Unix daemon/instance-management command line yet.

`run-windows.ps1 -Example shell` enables native session prompts: replace the
desktop Explorer process on launch only after Yes, and offer to start Explorer
after normal exit if its desktop is absent. Startup No preserves Explorer;
Cancel cancels launch. Explorer windows in the stopped desktop process close.
Our tray and windows are released before the restoration prompt. No Windows
logon settings are changed. `-NoExplorerPrompts` and timed verification launches
skip prompts; direct executable users can opt in with `--manage-explorer`.
Replacement uses a planned Restart Manager shutdown, waits for Explorer to
exit and checks for an immediate restart. It does not force-kill Explorer or
change auto-restart policy. Panels skip external AppBar calls while our own
tray receiver hosts `Shell_TrayWnd`, avoiding startup stalls. Without an
external AppBar host, they use the monitor rectangle without workspace
reservation. `src/windows/test/explorer-session.ps1 -ReplaceThenRestore` exercises
the actual Yes/restore path; it closes Explorer temporarily and restores it.

The example is `examples/windows-panel/shell.qml`. Edit it while running:
the upstream file watcher reloads the configuration automatically. Click
**Exit** or focus the example panel and press **Alt+F4**. QML applications
exit using `Qt.quit()`.

The example displays local date and time using upstream `SystemClock`, updated
once per second. This uses Qt's system clock access and needs no Windows service
process or new native backend. The time format is `HH:mm:ss`; the date format
is `ddd, dd MMM yyyy`.

## Full Windows example

`examples/windows-shell` adds an Omarchy-inspired desktop setup alongside the
minimal panel. Run `run-windows.ps1 -Example shell` for monitor bars, a searchable
Windows application launcher, calendar, native audio and battery panels, a
volume OSD, persistent appearance settings, and a control center linking to
Windows network, Bluetooth, display, wallpaper, notifications and Task View.
See [the example README](examples/windows-shell/README.md) for controls,
platform limitations and its smoke test. It uses the existing Windows build;
no additional native services or dependencies are required.
Open windows appear after the Applications button, grouped by process ID with
a window count. Click to restore the last window or cycle through an active
group; single-window groups toggle minimize. Right-click for the current
window's native menu, or press Win+W to
request normal closure while preserving save prompts.

## Windows power service

`import Quickshell.Windows` exposes the `PowerStatus` singleton. The sample
panel shows battery percentage, charging/AC state, and a battery saver label.
It highlights a low battery while on battery power. Reading system power
status is available without administrator privileges or a service process.

```qml
import QtQuick
import Quickshell.Windows

Text {
    text: PowerStatus.available && PowerStatus.batteryPercent >= 0
        ? PowerStatus.batteryPercent + "%"
        : "Battery charge unavailable"
}
```

| Property | Meaning |
| --- | --- |
| `available` | The last Windows power query succeeded; true also on desktops without batteries. |
| `acStatus` | `PowerStatus.AcOnline`, `AcOffline`, or `AcUnknown`. |
| `batteryPresence` | `PowerStatus.HasBattery`, `NoBattery`, or `BatteryUnknown`. |
| `batteryPercent` | Integer 0–100, or -1 when unknown/not applicable. |
| `charging` | Windows reports an installed battery charging; AC power alone does not imply charging. |
| `batterySaverEnabled` | Windows reports battery saver enabled. |
| `remainingSeconds` | Windows' estimate while on battery, or -1 when unknown/not applicable. |
| `errorString` | Query failure details; empty on success. |

`PowerStatus.refresh()` requests a reading immediately. All properties use
`statusChanged`, emitted only when the snapshot or error changes. On a query
failure, `available` becomes false, numeric readings return their unknown
values, and boolean readings become false. Check `available` before interpreting
them as hardware state. This is the aggregate system battery reading, not a
list of individual batteries or UPS devices.

The provider uses `GetSystemPowerStatus`, registers native power-source,
battery-percentage and battery-saver notifications on its own hidden top-level
window, and refreshes after resume. A 30-second timer provides a fallback.
It works without a visible panel. Each QML engine owns its provider, and
reloading unregisters and destroys the old notification window.

For the Windows power normalization tests, configure `-DBUILD_TESTING=ON`,
build, and run:

```powershell
& C:\Qt\Tools\CMake_64\bin\ctest.exe --test-dir build-quickshell `
    -R windows-power-status --output-on-failure
```

The tests cover unknown flags, invalid percentages, absent batteries,
charging, battery saver, empty batteries, AC without charging, and unavailable
readings. The desktop smoke test also checks the provider's lifetime across
QML reloads and panel visibility changes. Physical AC unplug/replug and
sleep/resume still require interactive hardware verification.

## Windows volume services

`import Quickshell.Windows` exposes two independent singletons over Windows Core
Audio, both sharing one implementation:

| Singleton | Endpoint |
| --- | --- |
| `Volume` | Default multimedia **playback** device |
| `InputVolume` | Default multimedia **recording** device |

They are deliberately separate rather than one object with a mode switch:
recording and playback defaults change independently, and neither endpoint's
state is derived from the other. Each follows the `eMultimedia` role, so a
separate communications endpoint set by a conferencing application is not
reported.

The panel's **-** and **+** buttons adjust `Volume` by five percentage points.
Click the volume text to toggle mute; scrolling over it adjusts two percentage
points per wheel notch. Controls are disabled when there is no usable audio
output.

```qml
import Quickshell.Windows

// Examples for event handlers, valid on both singletons:
Volume.volume = 0.5          // Set master volume to 50%.
Volume.adjustVolume(0.05)    // Raise it by five percentage points.
Volume.muted = true
Volume.toggleMuted()

InputVolume.adjustVolume(0.05) // The same, on the recording endpoint.
InputVolume.muted = true
```

| Property | Meaning |
| --- | --- |
| `available` | A default multimedia playback endpoint was acquired and read successfully. |
| `volume` | Read/write normalized master volume, 0–1; -1 when unavailable. Writes clamp to 0–1 and reject non-finite values. |
| `volumePercent` | Read-only rounded percentage, 0–100; -1 when unavailable. |
| `muted` | Read/write endpoint mute state. Check `available` before interpreting false. |
| `deviceId` | Current endpoint ID; empty when unavailable. |
| `deviceName` | Friendly output name when Windows supplies one. |
| `errorString` | Last query or control error; empty after a successful reading. |

`refresh()` requests a reading immediately. Setters read the device back after
successful writes. All properties notify through `statusChanged` only when
state or errors change. Muting preserves the stored volume level; volume
changes do not automatically unmute.

Core Audio notifications track changes made by Windows or other applications
and changes to the default device, including device addition/removal. The
COM callbacks post messages to a private hidden window; Qt reads and publishes
state on its GUI thread. Instance tokens protect against late messages after
QML reload. A five-second timer retries unavailable devices and provides a
notification fallback. COM registrations, references, apartments and hidden
windows are released when their QML engine is destroyed.

This service controls system master output, not individual application sessions,
microphone gain, or output-device selection. It follows the `eMultimedia` role;
the communications default can be a different device. It changes no settings
during startup or reload. Hardware exclusive-mode behavior can differ from
shared-mode master volume behavior.

With `BUILD_TESTING=ON`, the `windows-volume` CTest checks native
readings, invalid writes, controls, external callbacks and notification-window
lifetime. Its control test briefly changes volume/mute and restores both
original values, including on assertion failure. If there is no available
playback endpoint, the hardware-dependent cases are skipped. Physical output
switches, device unplug/replug and audio-service restart still need interactive
verification. The recording endpoint shares that implementation and is not
separately exercised.

## Current behavior

The full Windows example now includes an experimental native tray receiver
(`Quickshell.Windows.TrayHost`) that works when Explorer's tray is absent. It
hosts application icons in our bar and Tray panel, survives QML reload, and
routes legacy/version-4 clicks to applications. It leaves another running tray
host alone. See [the example README](examples/windows-shell/README.md) for
compatibility limits and its native `windows-tray-host` CTest. Windows shell
replacement is ongoing: this receiver does not yet implement Explorer's AppBar
service, desktop, task management or notification balloons.

- `import Quickshell` supplies a native Windows `PanelWindow` with the existing
  QML API, including anchors, margins, visibility, screen, implicit size,
  `exclusiveZone`, `exclusionMode`, `focusable`, and `aboveWindows`.
- AppBars use `ABM_NEW`, `ABM_QUERYPOS`, `ABM_SETPOS`, and `ABM_REMOVE`.
  Registration is released on hide, close, shutdown and window ownership
  transfer during reload. The backend handles position, display/DPI and
  Explorer restart notifications, and yields to fullscreen applications.
- Dimensions follow Quickshell's **logical pixel** convention. For example,
  `implicitHeight: 40` reserves 60 physical pixels at 150% scaling. The original
  standalone PoC still reserves 40 physical pixels.
- `ExclusionMode.Auto` reserves the implicit thickness plus margins for a
  single exclusion edge. `Normal` uses the explicit `exclusiveZone`;
  `Ignore` reserves nothing and positions against the monitor rectangle.
- Windows AppBars reserve an entire monitor edge even when the visible panel
  spans only part of it. This differs from compositor-specific partial zones.
- Window transfer during reload retains the native HWND when upstream's
  existing surface-format compatibility checks permit reuse.
- Portable Quickshell IO and widget modules are built. The process module
  uses `QProcess`; signal 15 maps to `terminate()` and 9 to `kill()` on Windows.
  Other Unix signals are rejected with a warning.
- Widget shaders include HLSL for Qt Quick's Windows D3D11 renderer.
- Windows startup uses Qt standard paths and a console logger with upstream
  category filtering and formatting. Local-file URL conversion supports drive
  letters and spaces.

## Verification

```powershell
.\src\windows\test\smoke.ps1
```

This opens temporary test panels and closes them automatically. It checks
workspace reservation at the current DPI, hot reload, HWND retention,
Ignore mode, hiding/showing, all four edges, process execution/stdout and
workspace restoration after exit. Test configurations and logs are stored in
`build-quickshell/windows-smoke`.

Multi-monitor moves, physical monitor hotplug, Explorer restart and real
fullscreen applications still need separate interactive verification.

## Windows settings activation

`Quickshell.Windows.System.openSettings(page)` validates the page name and
activates `ms-settings:` on a worker thread. Each request initializes COM on
that worker and balances its initialization before returning. COM initialized
on the GUI thread does not initialize a worker.

When Explorer's desktop is absent, the launcher now owns an experimental C++
Settings host in `quickshell-windows-immersive-host`, an isolated SDK/STL target.
Its STA registers a hidden shell window, starts Windows' immersive controller,
and obtains the view dispatcher through COM. It enumerates views and matches
the Settings frame instead of using the HWND lookup, whose map can still be
empty on first activation. It completes content discovery/visibility/layout
and removes shell cloaking from both the content and application frame.
If minimizing detaches content from a frame already verified as ready, it
restores that same frame/content pair and sizes the content to its client area.
Once the frame is ready, it maintains the gear icon from SystemSettings.exe
and enables standard window styles and DWM non-client rendering. The task row
refreshes icons assigned after window creation and resolves Settings' packaged
gear icon directly by app identity when the frame returns a generic icon.
Settings opens centered in the work area of the monitor containing the pointer;
centering runs once per opening and preserves maximized windows. Each successful
Settings URI activation requests foreground and content focus for up to five seconds,
including when an existing frame is reused, then stops requesting activation.
This fixes both `0x80040900` activation failures and the window stuck on a giant
gear. Wi-Fi and power activation passed with the host embedded in Quickshell,
no Explorer process and no diagnostic helper; the frame/content were attached,
visible and uncloaked. The user also confirmed usable Settings during the
native-host walkthrough. Merely invoking the same URI through PowerShell does
not supply these services and did not resolve the failure.

The host belongs to the launcher, survives QML reload, and releases its COM
objects, import redirect and registered shell window before the Explorer
restoration prompt. It leaves another registered desktop shell alone. No Rust
library or third-party host is linked or launched. Only this process's fallback
window import is redirected from the immersive background band to the ordinary
desktop band. The touch-keyboard shell component is omitted; touch-keyboard
support is not provided by this host. Presentation is scoped to Settings and
requires an unambiguous frame/content pair; general UWP application hosting
and multiple simultaneous Settings views are not verified.

Private Windows interfaces are not a stable SDK contract. Startup checks the
two exact DLL/PDB identities verified here: `twinui.pcshell.dll` file version
`10.0.26100.9278` and `Windows.ImmersiveShell.ServiceProvider.dll` file version
`10.0.26100.8117`. It also validates method addresses before calling them.
Other Windows revisions disable this host with a warning rather than calling
an unverified ABI; keep Explorer running on those revisions. Microsoft PDBs
are downloaded over HTTPS into Quickshell's per-user cache on first use
(about 34 MiB), then reused. First startup needs network access or a populated
cache; `QS_WINDOWS_SYMBOL_CACHE` overrides its directory. Download failures
leave the host unavailable and are reported. The GUI event loop remains
responsive while the host starts; QML loads after initialization completes.

Completion is delivered on the GUI thread and discarded if the singleton was
destroyed during a reload. Error logs now show native codes in hexadecimal.
The automated `windows-system` test covers invalid page names, nonexistent
targets and destruction during activation, without opening Settings. To check
real activation, run `windows-system.exe --open-settings` with Qt on `PATH`,
or use `src/windows/test/manual/settings-launch.qml`. These open the Wi-Fi and
power pages without changing settings or starting/stopping Explorer. Native
handlers still have no forced timeout; they run off the GUI thread.
The `windows-immersive-host` CTest verifies that an existing registered shell
prevents host initialization. It skips when no shell is registered. Its manual
probe links the same native backend as the launcher; it is not a runtime
dependency. With Explorer already absent and Settings closed, run
`windows-immersive-host-probe.exe <symbol-cache> --check-settings` to check
one initial Wi-Fi activation, restoring minimized Settings on the power page,
resizing content and reopening Settings. It asserts stable attachment,
visibility, uncloaking, centering, native icon, decorations, foreground and
keyboard focus. This mode does not stop Explorer and returns 77 while another
desktop shell is registered.

## Windows notification history

`Quickshell.Windows.NotificationHistory` exposes a read-only
`ObjectModel<WindowsNotification>` of toast notifications still retained by
Windows, newest first. The example's Control center opens **Notification
history**. Each item has `id`, `appName`, `summary`, `body`, and `created`.
Text is displayed as plain text, without actions or markup. No notification
is sent, dismissed or removed by this backend.

Reads run in a worker's WinRT MTA, with a five-second read timeout, and refresh
every ten seconds. `refresh()` reads on demand. `requestAccess()` starts the
Windows permission request on the UI thread, only after the user selects
**Allow notification access**. `available`, `access`, `busy`, and `errorString`
distinguish denied/unavailable access from an empty history. Revocation clears
the local model. Notification-change event registration is not used; polling
works without daemon mode or instance IPC.

The native desktop build was verified to read history and survive destruction
during a read. A sandboxed launch returned `0x80070057` (invalid parameter);
the example reports this rather than showing invented notifications. The panel
walkthrough read and rendered 17 retained notifications with access allowed.
Permission prompts and revocation through Settings still need interactive
verification. Windows controls access; see Microsoft's
[notification listener documentation](https://learn.microsoft.com/en-us/windows/apps/develop/notifications/app-notifications/notification-listener).

## Windows application discovery

`Quickshell.DesktopEntries` enumerates the Windows AppsFolder namespace using
a separate `quickshell-windows-app-scanner` static target. The target includes
only STL/Windows SDK headers and links `ole32` and `shell32`; its interface to
the core contains no SDK types. Enumeration initializes COM on the calling
thread and frees PIDLs, display-name strings and interfaces before leaving
that apartment. `DesktopEntryScanner::run()` calls this backend on Windows.

`id` contains the complete AppsFolder child identifier (including any
backslashes in desktop-app IDs), `name` is Windows' display name, and
`command`/`execute()` use separate Explorer arguments to activate it. Icon,
category and desktop-action metadata are not populated by this scanner.
`DesktopEntries.loading`, `errorString`, and `refresh()` expose scan status
and manual refresh. Startup enumeration runs off the GUI thread. Start menu
directory changes trigger scans, but packaged-app changes may require Refresh.

The example no longer starts PowerShell to discover applications. Two native
scanner builds and native enumeration/refresh tests passed; the native result
matched all 227 `Get-StartApps` IDs on this machine. Actual application launch
and installation/uninstallation changes still need interactive verification.
Linux continues to use XDG desktop entries and has not been tested here.

For automated checks without the interactive task-window test:

```powershell
$env:PATH = "C:\Qt\6.11.2\msvc2022_64\bin;" + $env:PATH
& C:\Qt\Tools\CMake_64\bin\ctest.exe --test-dir build-quickshell `
    --output-on-failure -E windows-task-windows
```

## Remaining port work

This is a working core/panel milestone, not a complete Windows desktop shell.
Wayland/X11/Hyprland/i3, Linux D-Bus services, PipeWire, PAM, Polkit, greetd,
UPower, Bluetooth and NetworkManager are disabled in Windows builds. The
upstream notification server remains disabled; Windows notification history
is a separate read-only service in `Quickshell.Windows`.

The Windows launcher does not start the instance IPC server or support IPC
commands, daemon mode, Unix startup pragmas, the Unix crash reporter, or
encoded file logging. The upstream Unix tooling VFS requires symlinks and is
explicitly disabled rather than emitting Windows `.lnk` shortcuts as QML
imports. These require subsequent Windows implementations.

The build scripts target MSVC, and Linux builds after these changes have not
been tested. Packaging Qt runtime dependencies is a later step.
