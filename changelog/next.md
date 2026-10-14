## Windows

- Added `examples/windows-shell`, an Omarchy-inspired desktop example with
  monitor bars, Windows application search, audio and battery panels, calendar,
  volume OSD, saved appearance settings and native Windows settings shortcuts.
  Select it with `run-windows.ps1 -Example shell`.
- Styled the full Windows example with Omarchy's Gruvbox palette, monospace
  typography, square controls and a compact bar.
- Added a process-owned Windows launcher hotkey: Win+Space with a
  Ctrl+Alt+Space fallback, including reload-safe registration and launcher focus.
- Added an experimental Windows tray host owned by Quickshell, with native
  application icon registration, a QML icon model and click callback delivery.
- Added Explorer replacement/restoration prompts to normal full-shell launches.
  Replacement requires Yes; startup No/Cancel leave Explorer alone, and exit
  restoration is offered only after Quickshell's own tray has been released.
- Fixed Explorer immediately restarting after replacement by requesting a
  planned Restart Manager shutdown. Panels avoid synchronous AppBar calls to
  Quickshell's own tray receiver while running as the desktop shell.
- Moved Windows settings URI activation to a native worker with its own COM
  apartment so opening Settings does not stall Quickshell's GUI thread.
- Added an embedded C++ immersive host that makes Settings usable without
  Explorer on the verified Windows DLL revisions, including completion of
  content layout and shell uncloaking. The host survives configuration reloads.
- Restored the Settings gear app icon and standard window decorations in the
  immersive host, and request foreground focus when Settings becomes ready.
- Resolve Settings' task icon from its packaged gear asset and center its
  window on the pointer's monitor when opening or activating a Settings page.
- Fix first Settings activation by finding its frame-owned view before the
  HWND lookup is ready, and restore content attachment after minimizing.
- Added a shell-owned wallpaper/background panel and monitor surfaces, with
  asynchronous images, solid colors, Fill/Fit and a non-native file picker.
- Moved button tooltips outside the bar using separate popup windows.
- Added a task row after the Applications button with native window icons,
  activate/minimize behavior, scrolling, and each app's native right-click menu.
- Added Win+W to request normal closure of the foreground window, with a scoped
  keyboard hook when Windows reserves the key and Ctrl+Alt+W as the fallback.
- Replaced the panels' native Windows decoration with Omarchy-style frameless
  popups that center under the bar, stay out of the taskbar and Alt+Tab, and
  dismiss when focus moves elsewhere.
- Fixed the task row's scroll wheel, which was parented to the scrolled content
  instead of the viewport and so never scrolled.
- Fixed the Windows tray host losing its receiver window: `windowProc` forwards
  everything but `WM_COPYDATA` to `DefWindowProc`, which destroys a window on
  `WM_CLOSE`, so a stray close left the bar rendering icons that could never
  update while still reporting success. The host now watches for its own
  destruction, clears those icons, reports the loss, and rebuilds the receiver on
  retry. Covered by a new `receiverLoss` test.
- Stopped some tray applications from opening their shortcut menus across the
  screen. The cursor anchor we send already matched the documented version-4
  layout but nothing verified it, so a test now pins `wParam` to the click
  point, and a menu watch pulls a `#32768` menu back under the pointer when an
  application places it more than 40 pixels away shortly after a tray click.
- Replaced the bar's Network and Tray labels with drawn icons, and replaced the
  battery text with an Omarchy-style battery outline whose fill tracks the level
  and gains a charging bolt. Removed labels moved to tooltips.
- Added a Network panel that reads the live connection the way Omarchy presents
  it: network name, status line, signal or Ethernet mark, and the adapter, MAC,
  link speed, IPv4 address, gateway, DNS and security. It reads
  `Quickshell.Networking`, which gained a Windows backend built on the IP helper
  and WLAN APIs, replacing the per-panel PowerShell process. Read-only, and
  joining and pairing stay in Windows settings.
- Stopped a bar button from staying selected after its popup closed: the panel
  now clears the active panel whenever it is no longer visible, so closing it
  any way releases the underline and highlight.
- Replaced the bar's volume percentage with a drawn speaker icon that crosses
  its waves out when muted, moving the level and device name to the tooltip.
- Split the Core Audio backend into `Volume` and `InputVolume`, so the default
  recording device has its own volume, mute and notifications instead of only
  playback being exposed.
