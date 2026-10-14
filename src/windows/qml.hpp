#pragma once

#include <qobject.h>
#include <qqmlintegration.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../core/doc.hpp"
#include "../core/model.hpp"
#include "launcher_shortcut.hpp"
#include "task_windows.hpp"
#include "tray_host.hpp"

///! Windows notification area (system tray) host.
/// The receiver window and the icons registered with it outlive QML reloads;
/// referencing this singleton starts the receiver when no other shell owns it.
/// Items are @@WindowsTrayItem objects, listed in @@items.
class TrayHost: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;
	QSDOC_TYPE_OVERRIDE(ObjectModel<WindowsTrayItem>*);
	Q_PROPERTY(UntypedObjectModel* items READ items CONSTANT);
	/// True while our receiver window owns the notification area.
	Q_PROPERTY(bool available READ available NOTIFY statusChanged);
	/// Why the receiver is unavailable, empty while it works.
	Q_PROPERTY(QString errorString READ errorString NOTIFY statusChanged);

public:
	explicit TrayHost(QObject* parent = nullptr);

	[[nodiscard]] static ObjectModel<WindowsTrayItem>* items() {
		return WindowsTrayHost::instance()->items();
	}
	[[nodiscard]] static bool available() { return WindowsTrayHost::instance()->available(); }
	[[nodiscard]] static QString errorString() { return WindowsTrayHost::instance()->errorString(); }

	/// Start our receiver if no other shell currently hosts the tray, and
	/// rebuild it if a previous receiver window was destroyed.
	Q_INVOKABLE static bool start();

signals:
	void statusChanged();
};

///! Task bar windows belonging to other applications.
/// Windows are enumerated by the process, so the list and its order survive
/// QML reloads. Entries are @@WindowsTaskGroup objects, one per running
/// application, listed in @@groups. The windows of every application are also
/// listed flat, without an owning group, in @@windows.
class TaskWindows: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;
	QSDOC_TYPE_OVERRIDE(ObjectModel<WindowsTaskGroup>*);
	Q_PROPERTY(UntypedObjectModel* groups READ groups CONSTANT);
	QSDOC_TYPE_OVERRIDE(ObjectModel<WindowsTaskWindow>*);
	Q_PROPERTY(UntypedObjectModel* windows READ windows CONSTANT);

public:
	explicit TaskWindows(QObject* parent = nullptr);

	[[nodiscard]] static ObjectModel<WindowsTaskGroup>* groups() {
		return WindowsTaskWindows::instance()->groups();
	}

	[[nodiscard]] static ObjectModel<WindowsTaskWindow>* windows() {
		return WindowsTaskWindows::instance()->windows();
	}

	/// Ask the foreground window's application to close it, preserving any
	/// save prompt or other veto the application applies.
	Q_INVOKABLE static void closeForeground();
};

///! Global shortcut that opens this shell's launcher.
/// Registration is owned by the process and survives QML reloads.
class LauncherShortcut: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;
	/// Whether the launcher has a registered global shortcut.
	Q_PROPERTY(bool available READ available CONSTANT);
	/// Actual registered combination (Win+Space or Ctrl+Alt+Space fallback).
	Q_PROPERTY(QString shortcutText READ shortcutText CONSTANT);
	/// Registration failure details, including why a fallback was selected.
	Q_PROPERTY(QString errorString READ errorString CONSTANT);
	/// Combination bound to closing the foreground window, empty if unbound.
	Q_PROPERTY(QString closeShortcutText READ closeShortcutText CONSTANT);

public:
	explicit LauncherShortcut(QObject* parent = nullptr);

	[[nodiscard]] static bool available() {
		return WindowsLauncherShortcut::instance()->available();
	}
	[[nodiscard]] static QString shortcutText() {
		return WindowsLauncherShortcut::instance()->shortcutText();
	}
	[[nodiscard]] static QString errorString() {
		return WindowsLauncherShortcut::instance()->errorString();
	}
	[[nodiscard]] static QString closeShortcutText() {
		return WindowsLauncherShortcut::instance()->closeShortcutText();
	}

signals:
	/// Sent when the launcher shortcut is pressed, with the screen to show it on.
	void activated(QString screenName);
	/// Sent when the close shortcut is pressed.
	void closeActivated();
};