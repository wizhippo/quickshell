#pragma once

#include <qlist.h>
#include <qobject.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <windows.h>

#include "../core/doc.hpp"
#include "../core/model.hpp"

class WindowsTaskGroup;
class WindowsTaskWindows;

///! An open top level window that can be activated from a task bar.
class WindowsTaskWindow: public QObject {
	Q_OBJECT;
	QML_NAMED_ELEMENT(WindowsTaskWindow);
	QML_UNCREATABLE("WindowsTaskWindows can only be acquired from TaskWindows");
	// clang-format off
	/// The window title, as reported by the application.
	Q_PROPERTY(QString title READ title NOTIFY titleChanged BINDABLE bindableTitle);
	/// A `data:` URL PNG of the window's icon.
	Q_PROPERTY(QString iconSource READ iconSource NOTIFY iconSourceChanged BINDABLE bindableIconSource);
	/// True while this window is the active one.
	Q_PROPERTY(bool active READ active NOTIFY activeChanged BINDABLE bindableActive);
	// clang-format on

public:
	explicit WindowsTaskWindow(HWND window, QObject* parent = nullptr);

	[[nodiscard]] HWND window() const { return this->mWindow; }
	QBindable<QString> bindableTitle() { return &this->bTitle; }
	[[nodiscard]] QString title() const { return this->bTitle; }
	QBindable<QString> bindableIconSource() { return &this->bIconSource; }
	[[nodiscard]] QString iconSource() const { return this->bIconSource; }
	QBindable<bool> bindableActive() { return &this->bActive; }
	[[nodiscard]] bool active() const { return this->bActive; }

	/// Raise this window, or minimize it when it is already foreground.
	Q_INVOKABLE void activate();

	/// Show the window's own system menu at the current cursor position. The
	/// application owns the menu, including any custom entries it adds.
	Q_INVOKABLE void showContextMenu();

	/// Ask the application to close, preserving its own save prompts.
	Q_INVOKABLE void requestClose();

	[[nodiscard]] bool eligible() const;

	/// Re-read the title, icon and focus state from the window.
	void refresh();

signals:
	void titleChanged();
	void iconSourceChanged();
	void activeChanged();

public:
	DWORD mProcess = 0;

private:
	HWND mWindow;
	HICON mIcon = nullptr;
	bool mSettingsFrame = false;
	// clang-format off
	Q_OBJECT_BINDABLE_PROPERTY(WindowsTaskWindow, QString, bTitle, &WindowsTaskWindow::titleChanged);
	Q_OBJECT_BINDABLE_PROPERTY(WindowsTaskWindow, QString, bIconSource, &WindowsTaskWindow::iconSourceChanged);
	Q_OBJECT_BINDABLE_PROPERTY(WindowsTaskWindow, bool, bActive, &WindowsTaskWindow::activeChanged);
	// clang-format on
};

///! One task bar button for all windows owned by a process.
///
/// Each running process contributes a single button whose @@windowCount windows
/// are listed in @@windows.
class WindowsTaskGroup: public QObject {
	Q_OBJECT;
	QML_NAMED_ELEMENT(WindowsTaskGroup);
	QML_UNCREATABLE("WindowsTaskGroups can only be acquired from TaskWindows");
	// clang-format off
	/// The windows belonging to this application, in task bar order.
	QSDOC_TYPE_OVERRIDE(ObjectModel<WindowsTaskWindow>*);
	Q_PROPERTY(UntypedObjectModel* windows READ windows CONSTANT);
	/// How many windows the application currently owns.
	Q_PROPERTY(int windowCount READ windowCount NOTIFY windowCountChanged BINDABLE bindableWindowCount);
	/// The application name, falling back to its most recent window title.
	Q_PROPERTY(QString title READ title NOTIFY titleChanged BINDABLE bindableTitle);
	/// A `data:` URL PNG shared by the application's windows.
	Q_PROPERTY(QString iconSource READ iconSource NOTIFY iconSourceChanged BINDABLE bindableIconSource);
	/// True while any window of the application is the active one.
	Q_PROPERTY(bool active READ active NOTIFY activeChanged BINDABLE bindableActive);
	// clang-format on

public:
	explicit WindowsTaskGroup(DWORD process, QObject* parent = nullptr);

	[[nodiscard]] DWORD process() const { return this->mProcess; }
	[[nodiscard]] ObjectModel<WindowsTaskWindow>* windows() { return &this->mWindows; }
	QBindable<int> bindableWindowCount() { return &this->bWindowCount; }
	[[nodiscard]] int windowCount() const { return this->bWindowCount; }
	QBindable<QString> bindableTitle() { return &this->bTitle; }
	[[nodiscard]] QString title() const { return this->bTitle; }
	QBindable<QString> bindableIconSource() { return &this->bIconSource; }
	[[nodiscard]] QString iconSource() const { return this->bIconSource; }
	QBindable<bool> bindableActive() { return &this->bActive; }
	[[nodiscard]] bool active() const { return this->bActive; }

	/// The window a click applies to: the focused one, else the most recently
	/// used one, else the first one that is not minimized.
	[[nodiscard]] WindowsTaskWindow* current();

	/// Focus the next window of the application, wrapping around.
	///
	/// A single-window application instead toggles between foreground and
	/// minimized, matching a plain task bar button.
	Q_INVOKABLE void activate();

	/// Show the system menu of the window a click applies to.
	Q_INVOKABLE void showContextMenu();

	/// Ask every window's application to close, preserving save prompts.
	Q_INVOKABLE void requestCloseAll();

	/// Replace this group's windows with those of a fresh enumeration.
	void update(QList<HWND> windows);

signals:
	void windowCountChanged();
	void titleChanged();
	void iconSourceChanged();
	void activeChanged();

private:
	DWORD mProcess;
	// The window a click applies to, remembered across enumerations.
	WindowsTaskWindow* mCurrent = nullptr;
	QString mProcessName;
	ObjectModel<WindowsTaskWindow> mWindows {this};
	// clang-format off
	Q_OBJECT_BINDABLE_PROPERTY(WindowsTaskGroup, int, bWindowCount, &WindowsTaskGroup::windowCountChanged);
	Q_OBJECT_BINDABLE_PROPERTY(WindowsTaskGroup, QString, bTitle, &WindowsTaskGroup::titleChanged);
	Q_OBJECT_BINDABLE_PROPERTY(WindowsTaskGroup, QString, bIconSource, &WindowsTaskGroup::iconSourceChanged);
	Q_OBJECT_BINDABLE_PROPERTY(WindowsTaskGroup, bool, bActive, &WindowsTaskGroup::activeChanged);
	// clang-format on
};

/// Process-owned task bar window list.
///
/// Enumeration is owned by the process so that a QML reload does not restart
/// the poll or drop the window ordering. `Quickshell.Windows.TaskWindows` is
/// the per-engine QML facade over it. Windows are listed both flat in
/// @@windows and grouped per application in @@groups.
class WindowsTaskWindows: public QObject {
	Q_OBJECT;

public:
	explicit WindowsTaskWindows(QObject* parent = nullptr);

	static WindowsTaskWindows* instance();

	[[nodiscard]] ObjectModel<WindowsTaskGroup>* groups() { return &this->mGroups; }
	/// Every open window, in task bar order, across all @@groups.
	[[nodiscard]] ObjectModel<WindowsTaskWindow>* windows();

	/// Ask the foreground window to close, preserving save prompts.
	Q_INVOKABLE void closeForeground();

	static bool eligible(HWND window);

	/// Post a close request to a window, respecting application vetoes.
	static bool requestClose(HWND window);

public slots:
	void refresh();

private:
	ObjectModel<WindowsTaskGroup> mGroups {this};
	// Windows are owned by their group, so the flat list only borrows them.
	ObjectModel<WindowsTaskWindow> mWindows {this};
	QTimer timer;
};
