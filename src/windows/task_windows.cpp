#include "task_windows.hpp"

#include <dwmapi.h>
#include <qbuffer.h>
#include <qfileinfo.h>
#include <qguiapplication.h>
#include <qhash.h>
#include <qimage.h>

#include "settings_window.hpp"

namespace {
HICON nativeWindowIcon(HWND window) {
	DWORD_PTR value = 0;
	SendMessageTimeoutW(
	    window,
	    WM_GETICON,
	    ICON_SMALL2,
	    0,
	    SMTO_ABORTIFHUNG | SMTO_BLOCK,
	    20,
	    &value
	);
	if (!value)
		SendMessageTimeoutW(
		    window,
		    WM_GETICON,
		    ICON_SMALL,
		    0,
		    SMTO_ABORTIFHUNG | SMTO_BLOCK,
		    20,
		    &value
		);
	if (!value)
		SendMessageTimeoutW(window, WM_GETICON, ICON_BIG, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 20, &value);
	if (!value) value = GetClassLongPtrW(window, GCLP_HICONSM);
	if (!value) value = GetClassLongPtrW(window, GCLP_HICON);
	auto icon = reinterpret_cast<HICON>(value);
	if (!icon) icon = LoadIconW(nullptr, IDI_APPLICATION);
	return icon;
}

QString windowIcon(HICON icon) {
	auto copy = CopyIcon(icon);
	if (!copy) return {};
	auto image = QImage::fromHICON(copy);
	DestroyIcon(copy);
	QByteArray bytes;
	QBuffer buffer(&bytes);
	buffer.open(QIODevice::WriteOnly);
	image.save(&buffer, "PNG");
	return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64());
}

DWORD windowProcess(HWND window) {
	DWORD process = 0;
	GetWindowThreadProcessId(window, &process);
	return process;
}

// The foreground window is queried directly rather than read from the cached
// per-window state, so clicking twice quickly still cycles.
HWND foregroundRoot() { return GetAncestor(GetForegroundWindow(), GA_ROOT); }
} // namespace

bool WindowsTaskWindows::eligible(HWND window) {
	if (!IsWindow(window) || !IsWindowVisible(window) || window == GetShellWindow()) return false;
	DWORD process = 0;
	GetWindowThreadProcessId(window, &process);
	if (!process || process == GetCurrentProcessId()) return false;
	const auto style = GetWindowLongPtrW(window, GWL_EXSTYLE);
	if (style & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) return false;
	if (GetWindow(window, GW_OWNER) && !(style & WS_EX_APPWINDOW)) return false;
	wchar_t cls[256] {};
	GetClassNameW(window, cls, 256);
	const QString name = QString::fromWCharArray(cls);
	if (name == "Progman" || name == "WorkerW" || name.startsWith("Shell_")) return false;
	DWORD cloaked = 0;
	DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
	return !cloaked && GetWindowTextLengthW(window) > 0;
}

WindowsTaskWindow::WindowsTaskWindow(HWND window, QObject* parent)
    : QObject(parent)
    , mWindow(window) {}

bool WindowsTaskWindow::eligible() const { return WindowsTaskWindows::eligible(this->mWindow); }

void WindowsTaskWindow::refresh() {
	wchar_t title[2048] {};
	GetWindowTextW(this->mWindow, title, 2048);
	this->bindableTitle().setValue(QString::fromWCharArray(title));
	const DWORD process = windowProcess(this->mWindow);
	// UWP frames acquire their icon after becoming visible. Refresh when the
	// native handle changes rather than retaining the initial generic icon.
	const auto icon = nativeWindowIcon(this->mWindow);
	const bool settingsFrame = qs::windows::settings::isFrame(this->mWindow);
	if (this->iconSource().isEmpty() || this->mProcess != process || this->mIcon != icon
	    || this->mSettingsFrame != settingsFrame)
	{
		// ApplicationFrameHost can ignore WM_SETICON or return a generic class icon.
		// Resolve Settings by app identity, independently of its title and host PID.
		QImage image;
		if (settingsFrame)
			image.load(QString::fromStdWString(qs::windows::settings::iconPath().wstring()));
		if (!image.isNull()) {
			QByteArray bytes;
			QBuffer buffer(&bytes);
			buffer.open(QIODevice::WriteOnly);
			image.save(&buffer, "PNG");
			this->bindableIconSource().setValue(
			    QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64())
			);
		} else {
			this->bindableIconSource().setValue(windowIcon(icon));
		}
	}
	this->mSettingsFrame = settingsFrame;
	this->mIcon = icon;
	this->mProcess = process;
	this->bindableActive().setValue(this->mWindow == foregroundRoot());
}

void WindowsTaskWindow::activate() {
	if (!this->eligible()) return;
	if (this->mWindow == GetForegroundWindow()) {
		PostMessageW(this->mWindow, WM_SYSCOMMAND, SC_MINIMIZE, 0);
		return;
	}
	if (IsIconic(this->mWindow)) ShowWindowAsync(this->mWindow, SW_RESTORE);
	SetForegroundWindow(this->mWindow);
}

void WindowsTaskWindow::showContextMenu() {
	if (!this->eligible()) return;
	if (IsIconic(this->mWindow)) ShowWindowAsync(this->mWindow, SW_RESTORE);
	SetForegroundWindow(this->mWindow);
	POINT point {};
	GetCursorPos(&point);
	// The target app owns and handles its system menu, including custom entries.
	PostMessageW(this->mWindow, WM_SYSCOMMAND, SC_MOUSEMENU, MAKELPARAM(point.x, point.y));
}

void WindowsTaskWindow::requestClose() { WindowsTaskWindows::requestClose(this->mWindow); }

WindowsTaskGroup::WindowsTaskGroup(DWORD process, QObject* parent)
    : QObject(parent)
    , mProcess(process) {
	// Reading the executable names the button, and is done once per
	// application rather than on every poll. It may be denied by process
	// protection, in which case the window title is used instead.
	auto handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process);
	if (!handle) return;
	wchar_t path[MAX_PATH * 2] {};
	DWORD size = MAX_PATH * 2;
	if (QueryFullProcessImageNameW(handle, 0, path, &size))
		this->mProcessName = QFileInfo(QString::fromWCharArray(path)).completeBaseName();
	CloseHandle(handle);
}

WindowsTaskWindow* WindowsTaskGroup::current() {
	auto windows = this->mWindows.valueList();
	if (windows.isEmpty()) return nullptr;
	auto foreground = foregroundRoot();
	for (auto* window: windows)
		if (window->window() == foreground) return this->mCurrent = window;
	if (this->mCurrent && windows.contains(this->mCurrent)) return this->mCurrent;
	for (auto* window: windows)
		if (!IsIconic(window->window())) return this->mCurrent = window;
	return windows.first();
}

void WindowsTaskGroup::activate() {
	auto windows = this->mWindows.valueList();
	if (windows.isEmpty()) return;
	if (windows.length() == 1) {
		windows.first()->activate();
		return;
	}
	auto* current = this->current();
	auto index = windows.indexOf(current);
	// Return to the last window when the application is inactive. Clicking
	// an already active application advances through its windows.
	auto* next = current && current->window() != foregroundRoot()
	               ? current
	               : windows.at((index + 1) % windows.length());
	next->activate();
	// Cycling must not depend on the poll having noticed the new focus yet.
	this->mCurrent = next;
}

void WindowsTaskGroup::showContextMenu() {
	if (auto* window = this->current()) window->showContextMenu();
}

void WindowsTaskGroup::requestCloseAll() {
	for (auto* window: this->mWindows.valueList()) window->requestClose();
}

void WindowsTaskGroup::update(QList<HWND> windows) {
	QList<WindowsTaskWindow*> next;
	// Retain button order when foreground/z-order changes.
	for (auto* item: this->mWindows.valueList()) {
		if (windows.removeOne(item->window())) next.append(item);
		else item->deleteLater();
	}
	for (auto window: windows) next.append(new WindowsTaskWindow(window, this));
	this->mWindows.diffUpdate(next);
	this->bindableWindowCount().setValue(next.length());

	QString iconSource;
	bool active = false;
	auto foreground = foregroundRoot();
	for (auto* item: next) {
		item->refresh();
		if (iconSource.isEmpty()) iconSource = item->iconSource();
		active = active || item->window() == foreground;
	}

	if (!this->mCurrent || !next.contains(this->mCurrent)) this->mCurrent = nullptr;
	// The app name is stable, so it is preferred over any single window title.
	auto* current = this->current();
	this->bindableTitle().setValue(
	    (current && qs::windows::settings::isFrame(current->window())) ? current->title()
	    : this->mProcessName.isEmpty() ? (current ? current->title() : QString())
	                                   : this->mProcessName
	);
	this->bindableIconSource().setValue(iconSource);
	this->bindableActive().setValue(active);
}

WindowsTaskWindows::WindowsTaskWindows(QObject* parent): QObject(parent) {
	connect(&timer, &QTimer::timeout, this, &WindowsTaskWindows::refresh);
	timer.start(750);
	this->refresh();
}

WindowsTaskWindows* WindowsTaskWindows::instance() {
	// Enumeration and button order outlive QML reloads.
	static auto* instance = new WindowsTaskWindows(qGuiApp); // NOLINT
	return instance;
}

ObjectModel<WindowsTaskWindow>* WindowsTaskWindows::windows() { return &this->mWindows; }

void WindowsTaskWindows::refresh() {
	QList<HWND> handles;
	EnumWindows(
	    [](HWND window, LPARAM context) -> BOOL {
		    if (eligible(window)) reinterpret_cast<QList<HWND>*>(context)->append(window);
		    return TRUE;
	    },
	    reinterpret_cast<LPARAM>(&handles)
	);

	// Bucket by application. A process keeps the position of its topmost
	// window, so the button order follows the task bar rather than the
	// order processes happen to be discovered in.
	QList<DWORD> processes;
	QHash<DWORD, QList<HWND>> byProcess;
	for (auto window: handles) {
		const auto process = windowProcess(window);
		if (!byProcess.contains(process)) processes.append(process);
		byProcess[process].append(window);
	}

	QHash<DWORD, WindowsTaskGroup*> retained;
	QList<WindowsTaskGroup*> groups;
	// Retain button order when foreground/z-order changes.
	for (auto* group: this->mGroups.valueList()) {
		if (!byProcess.contains(group->process())) {
			// Windows are children of their group, so they go with it.
			group->deleteLater();
			continue;
		}
		retained.insert(group->process(), group);
		groups.append(group);
	}
	for (auto process: processes) {
		if (retained.contains(process)) continue;
		auto* group = new WindowsTaskGroup(process, this);
		retained.insert(process, group);
		groups.append(group);
	}

	QList<WindowsTaskWindow*> windows;
	for (auto* group: groups) {
		group->update(byProcess.value(group->process()));
		windows.append(group->windows()->valueList());
	}

	this->mGroups.diffUpdate(groups);
	this->mWindows.diffUpdate(windows);
}

void WindowsTaskWindows::closeForeground() { requestClose(foregroundRoot()); }

bool WindowsTaskWindows::requestClose(HWND window) {
	return eligible(window) && PostMessageW(window, WM_CLOSE, 0, 0);
}
