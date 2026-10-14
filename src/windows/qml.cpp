#include "qml.hpp"

TrayHost::TrayHost(QObject* parent): QObject(parent) {
	connect(WindowsTrayHost::instance(), &WindowsTrayHost::statusChanged, this, &TrayHost::statusChanged);
}

bool TrayHost::start() {
	return WindowsTrayHost::instance()->start();
}

TaskWindows::TaskWindows(QObject* parent): QObject(parent) {}

void TaskWindows::closeForeground() {
	WindowsTaskWindows::instance()->closeForeground();
}

LauncherShortcut::LauncherShortcut(QObject* parent): QObject(parent) {
	auto* shortcut = WindowsLauncherShortcut::instance();
	connect(shortcut, &WindowsLauncherShortcut::activated, this, &LauncherShortcut::activated);
	connect(shortcut, &WindowsLauncherShortcut::closeActivated, this, &LauncherShortcut::closeActivated);
}