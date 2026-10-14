name = "Quickshell.Windows"
description = "Windows platform APIs"
headers = [
	"qml.hpp",
	"tray_host.hpp",
	"task_windows.hpp",
	"launcher_shortcut.hpp",
	"endpointvolume.hpp",
	"system.hpp",
	"notification_history.hpp",
	"volume.hpp",
	"inputvolume.hpp",
	"power_status.hpp",
]
-----
This module exposes Windows platform functionality that has no cross-platform
equivalent in Quickshell.

The notification area, task bar and launcher shortcut are owned by the process
rather than the QML engine, so their native windows and registrations survive
reload. Referencing @@TrayHost, @@TaskWindows or @@LauncherShortcut from a
configuration that is reloaded will not drop what Windows already knows about
this shell.

@@Volume and @@PowerStatus are per-engine instead: each QML engine owns one
instance and its native notification registrations, so reloading leaves no
stale callbacks behind.

Network state is not here. It is served by @@Quickshell.Networking, which has
a Windows backend, so shells should prefer that module.

@@NotificationHistory reads toast notifications currently retained by Windows,
using C++/WinRT. @@WindowsNotification exposes application name, summary, body
and creation time. Access requests are explicit and user-triggered; the initial
read and ten-second refresh never show a permission dialog. History provides
no send, dismiss, clear, or action methods. It is per-engine, and outstanding
worker results are discarded when that engine is destroyed.
