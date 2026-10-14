#include "panel_window.hpp"
#include <algorithm>
#include <cmath>
#include <QGuiApplication>
#include <QPlatformSurfaceEvent>
#include <QQmlEngine>
#include <QScreen>
#include <QTimer>

namespace {
bool externalAppBarHostAvailable() {
	const auto tray = FindWindowW(L"Shell_TrayWnd", nullptr);
	DWORD owner = 0;
	if (tray) GetWindowThreadProcessId(tray, &owner);
	// Our tray receiver implements notification icons, not the AppBar protocol.
	// Sending synchronous SHAppBarMessage calls to it can block shell startup.
	return owner != 0 && owner != GetCurrentProcessId();
}
}

WinPanelWindow::WinPanelWindow(WinPanelInterface* owner)
    : ProxyWindowBase(owner), owner(owner) {
	callback = RegisterWindowMessageW(L"Quickshell.Windows.AppBarCallback");
	taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
	qGuiApp->installNativeEventFilter(this);
	connect(qGuiApp, &QCoreApplication::aboutToQuit, this, &WinPanelWindow::unregisterBar);
	connect(this, &ProxyWindowBase::implicitWidthChanged, this, &WinPanelWindow::schedulePosition);
	connect(this, &ProxyWindowBase::implicitHeightChanged, this, &WinPanelWindow::schedulePosition);
	connect(this, &ProxyWindowBase::devicePixelRatioChanged, this, &WinPanelWindow::schedulePosition);
	connect(this, &ProxyWindowBase::screenChanged, this, &WinPanelWindow::schedulePosition);
}

WinPanelWindow::~WinPanelWindow() {
	unregisterBar();
	qGuiApp->removeNativeEventFilter(this);
}

ProxiedWindow* WinPanelWindow::createQQuickWindow() {
	auto* result = new ProxiedWindow(this);
	result->setFlags(Qt::Tool | Qt::FramelessWindowHint);
	result->setTitle("Quickshell Windows Panel");
	return result;
}

void WinPanelWindow::connectWindow() {
	ProxyWindowBase::connectWindow();
	connect(window, &QWindow::visibleChanged, this, [this](bool visible) {
		if (!visible) unregisterBar();
		else schedulePosition();
	});
}

void WinPanelWindow::completeWindow() {
	ProxyWindowBase::completeWindow();
	positionPanel();
}

ProxiedWindow* WinPanelWindow::disownWindow(bool keepItemOwnership) {
	// A reload transfers the same QQuickWindow to a different proxy. The old
	// native event filter must stop handling it before the new owner registers.
	unregisterBar();
	hwnd = nullptr;
	return ProxyWindowBase::disownWindow(keepItemOwnership);
}

void WinPanelWindow::setVisibleDirect(bool visible) {
	if (!visible) unregisterBar();
	ProxyWindowBase::setVisibleDirect(visible);
	if (visible) positionPanel();
}

void WinPanelWindow::trySetWidth(qint32) { schedulePosition(); }
void WinPanelWindow::trySetHeight(qint32) { schedulePosition(); }

void WinPanelWindow::setScreen(QuickshellScreenInfo* screen) {
	unregisterBar();
	ProxyWindowBase::setScreen(screen);
	schedulePosition();
}

APPBARDATA WinPanelWindow::data() const {
	APPBARDATA result{};
	result.cbSize = sizeof(result);
	result.hWnd = hwnd;
	return result;
}

void WinPanelWindow::unregisterBar() {
	if (!registered) return;
	registered = false;
	auto abd = data();
	if (externalAppBarHostAvailable()) SHAppBarMessage(ABM_REMOVE, &abd);
	lastReservation = {};
}

HMONITOR WinPanelWindow::targetMonitor() const {
	struct Search { QString name; HMONITOR monitor = nullptr; } search{
		window->screen() ? window->screen()->name() : QString()
	};
	EnumDisplayMonitors(nullptr, nullptr,
		[](HMONITOR monitor, HDC, LPRECT, LPARAM argument) -> BOOL {
			auto* search = reinterpret_cast<Search*>(argument);
			MONITORINFOEXW info{};
			info.cbSize = sizeof(info);
			if (GetMonitorInfoW(monitor, &info)
			    && search->name.compare(QString::fromWCharArray(info.szDevice), Qt::CaseInsensitive) == 0) {
				search->monitor = monitor;
				return FALSE;
			}
			return TRUE;
		}, reinterpret_cast<LPARAM>(&search));
	return search.monitor ? search.monitor : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
}

void WinPanelWindow::applyStyles() {
	auto style = GetWindowLongPtrW(hwnd, GWL_STYLE);
	style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
	SetWindowLongPtrW(hwnd, GWL_STYLE, style);
	auto extended = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
	extended = (extended | WS_EX_TOOLWINDOW) & ~WS_EX_APPWINDOW;
	if (owner->focusable()) extended &= ~WS_EX_NOACTIVATE;
	else extended |= WS_EX_NOACTIVATE;
	SetWindowLongPtrW(hwnd, GWL_EXSTYLE, extended);
}

void WinPanelWindow::schedulePosition() {
	if (positioning || pending || !window || !mVisible) return;
	pending = true;
	QTimer::singleShot(0, this, [this] {
		pending = false;
		positionPanel();
	});
}

void WinPanelWindow::positionPanel() {
	if (!window || !mVisible || positioning) return;
	positioning = true;
	const auto newHandle = reinterpret_cast<HWND>(window->winId());
	if (newHandle != hwnd) {
		unregisterBar();
		hwnd = newHandle;
	}
	MONITORINFO info{};
	info.cbSize = sizeof(info);
	if (!hwnd || !GetMonitorInfoW(targetMonitor(), &info)) {
		positioning = false;
		qWarning() << "Could not find Windows panel monitor:" << GetLastError();
		return;
	}
	applyStyles();
	const auto a = owner->anchors();
	const auto m = owner->margins();
	const auto edge = a.exclusionEdge();
	const auto ratio = window->screen()->devicePixelRatio();
	auto pixels = [ratio](qreal logical) { return static_cast<LONG>(std::lround(logical * ratio)); };
	const auto desiredWidth = std::max<LONG>(1, pixels(implicitWidth()));
	const auto desiredHeight = std::max<LONG>(1, pixels(implicitHeight()));
	LONG reservation = 0;
	if (edge && owner->exclusionMode() != ExclusionMode::Ignore) {
		if (owner->exclusionMode() == ExclusionMode::Normal) {
			reservation = std::max<LONG>(0, pixels(owner->exclusiveZone()));
		} else if (edge == Qt::TopEdge || edge == Qt::BottomEdge) {
			reservation = std::max<LONG>(0, desiredHeight + pixels(m.top + m.bottom));
		} else {
			reservation = std::max<LONG>(0, desiredWidth + pixels(m.left + m.right));
		}
	}

	const bool appBarHost = externalAppBarHostAvailable();
	RECT available = owner->exclusionMode() == ExclusionMode::Ignore || !appBarHost ? info.rcMonitor : info.rcWork;
	if (reservation > 0 && appBarHost) {
		if (!registered) {
			auto abd = data();
			abd.uCallbackMessage = callback;
			registered = callback && SHAppBarMessage(ABM_NEW, &abd) != 0;
			if (!registered) qWarning() << "Windows AppBar registration failed.";
		}
		if (registered) {
			auto abd = data();
			abd.uEdge = edge == Qt::TopEdge ? ABE_TOP : edge == Qt::BottomEdge ? ABE_BOTTOM
			            : edge == Qt::LeftEdge ? ABE_LEFT : ABE_RIGHT;
			abd.rc = info.rcMonitor;
			SHAppBarMessage(ABM_QUERYPOS, &abd);
			available = abd.rc;
			switch (abd.uEdge) {
			case ABE_TOP: abd.rc.bottom = abd.rc.top + reservation; break;
			case ABE_BOTTOM: abd.rc.top = abd.rc.bottom - reservation; break;
			case ABE_LEFT: abd.rc.right = abd.rc.left + reservation; break;
			case ABE_RIGHT: abd.rc.left = abd.rc.right - reservation; break;
			}
			// Avoid asynchronous ABN_POSCHANGED -> ABM_SETPOS notification loops.
			if (!EqualRect(&abd.rc, &lastReservation) || abd.uEdge != lastEdge) {
				SHAppBarMessage(ABM_SETPOS, &abd);
				lastReservation = abd.rc;
				lastEdge = abd.uEdge;
			}
			switch (abd.uEdge) {
			case ABE_TOP: available.top = lastReservation.top; break;
			case ABE_BOTTOM: available.bottom = lastReservation.bottom; break;
			case ABE_LEFT: available.left = lastReservation.left; break;
			case ABE_RIGHT: available.right = lastReservation.right; break;
			}
		}
	} else {
		unregisterBar();
		// Removing our reservation may have updated rcWork synchronously.
		GetMonitorInfoW(targetMonitor(), &info);
		available = owner->exclusionMode() == ExclusionMode::Ignore || !appBarHost ? info.rcMonitor : info.rcWork;
	}

	const auto left = available.left + pixels(m.left);
	const auto right = available.right - pixels(m.right);
	const auto top = available.top + pixels(m.top);
	const auto bottom = available.bottom - pixels(m.bottom);
	const auto width = a.horizontalConstraint() ? std::max<LONG>(1, right - left) : desiredWidth;
	const auto height = a.verticalConstraint() ? std::max<LONG>(1, bottom - top) : desiredHeight;
	const auto x = a.mLeft ? left : a.mRight ? right - width : left + (right - left - width) / 2;
	const auto y = a.mTop ? top : a.mBottom ? bottom - height : top + (bottom - top - height) / 2;
	const auto z = fullscreen || !owner->aboveWindows() ? HWND_BOTTOM : HWND_TOPMOST;
	if (!SetWindowPos(hwnd, z, x, y, width, height, SWP_NOACTIVATE))
		qWarning() << "Could not position Windows panel:" << GetLastError();
	positioning = false;
}

bool WinPanelWindow::nativeEventFilter(const QByteArray& type, void* message, qintptr* result) {
	if (type != "windows_generic_MSG" && type != "windows_dispatcher_MSG") return false;
	const auto* msg = static_cast<MSG*>(message);
	if (!hwnd || msg->hwnd != hwnd) return false;
	if (msg->message == callback) {
		if (msg->wParam == ABN_POSCHANGED) schedulePosition();
		else if (msg->wParam == ABN_FULLSCREENAPP) {
			fullscreen = msg->lParam != 0;
			schedulePosition();
		}
		if (result) *result = 0;
		return true;
	}
	if (msg->message == taskbarCreated) {
		registered = false;
		lastReservation = {};
		schedulePosition();
	} else if (msg->message == WM_DISPLAYCHANGE || msg->message == WM_DPICHANGED) {
		schedulePosition();
	} else if (msg->message == WM_MOUSEACTIVATE && !owner->focusable()) {
		if (result) *result = MA_NOACTIVATE;
		return true;
	} else if ((msg->message == WM_ACTIVATE || msg->message == WM_WINDOWPOSCHANGED)
	           && registered && !notifyingShell) {
		notifyingShell = true;
		auto abd = data();
		if (externalAppBarHostAvailable())
			SHAppBarMessage(msg->message == WM_ACTIVATE ? ABM_ACTIVATE : ABM_WINDOWPOSCHANGED, &abd);
		notifyingShell = false;
	} else if (msg->message == WM_CLOSE || msg->message == WM_DESTROY) {
		unregisterBar();
	}
	return false;
}

WinPanelInterface::WinPanelInterface(QObject* parent)
    : PanelWindowInterface(parent), panel(new WinPanelWindow(this)) {
	connectSignals();
}

void WinPanelInterface::onReload(QObject* oldInstance) {
	QQmlEngine::setContextForObject(panel, QQmlEngine::contextForObject(this));
	auto* old = qobject_cast<WinPanelInterface*>(oldInstance);
	panel->reload(old ? old->panel : nullptr);
}

ProxyWindowBase* WinPanelInterface::proxyWindow() const { return panel; }

void WinPanelInterface::setAnchors(Anchors value) {
	if (value == mAnchors) return;
	mAnchors = value;
	emit anchorsChanged();
	panel->schedulePosition();
}
void WinPanelInterface::setMargins(Margins value) {
	if (value == mMargins) return;
	mMargins = value;
	emit marginsChanged();
	panel->schedulePosition();
}
void WinPanelInterface::setExclusiveZone(qint32 value) {
	if (mExclusiveZone != value) {
		mExclusiveZone = value;
		emit exclusiveZoneChanged();
	}
	setExclusionMode(ExclusionMode::Normal);
	panel->schedulePosition();
}
void WinPanelInterface::setExclusionMode(ExclusionMode::Enum value) {
	if (value == mExclusionMode) return;
	mExclusionMode = value;
	emit exclusionModeChanged();
	panel->schedulePosition();
}
void WinPanelInterface::setAboveWindows(bool value) {
	if (value == mAboveWindows) return;
	mAboveWindows = value;
	emit aboveWindowsChanged();
	panel->schedulePosition();
}
void WinPanelInterface::setFocusable(bool value) {
	if (value == mFocusable) return;
	mFocusable = value;
	emit focusableChanged();
	panel->schedulePosition();
}
