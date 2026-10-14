#include "tray_host.hpp"
#include <cstring>

#include <qbuffer.h>
#include <qguiapplication.h>
#include <qhash.h>
#include <qimage.h>
#include <qlist.h>
#include <shellapi.h>

namespace {
constexpr wchar_t TrayClass[] = L"Shell_TrayWnd";
constexpr wchar_t NotifyClass[] = L"TrayNotifyWnd";
struct TrayPacket {
	DWORD signature, operation;
	TrayWireIcon icon;
};
static_assert(sizeof(TrayWireIcon) == 956);
// WM_COPYDATA is the cross-process trust boundary. Keep SEH in this leaf
// helper: only POD and raw reads/copies, with no C++ stack unwinding required.
bool copyTrayPacket(const COPYDATASTRUCT* source, TrayPacket* packet) {
	__try {
		const COPYDATASTRUCT copy = *source;
		if (copy.dwData != 1 || !copy.lpData || copy.cbData < sizeof(TrayPacket)) return false;
		std::memcpy(packet, copy.lpData, sizeof(*packet));
		return packet->icon.size >= sizeof(TrayWireIcon);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}
QString boundedText(const WCHAR* text, size_t capacity) {
	size_t length = 0;
	while (length < capacity && text[length]) ++length;
	return QString::fromWCharArray(text, static_cast<int>(length));
}

// A tray application's menu belongs to the application, which decides where to
// put it. Most read the anchor we send, but some take coordinates from the
// wrong parameter or mis-scale them, and land across the screen instead of
// under the pointer. Watch for a menu opening just after a tray click and pull
// it back only when it is clearly somewhere the user did not ask for.
constexpr DWORD MenuRepositionWindow = 2000;
constexpr int MenuRepositionTolerance = 40;
WindowsTrayHost* menuWatchOwner = nullptr;
bool isStandardMenu(HWND window) {
	wchar_t className[64] {};
	GetClassNameW(window, className, 64);
	return className[0] == L'#' && wcscmp(className + 1, L"32768") == 0;
}
void CALLBACK menuWatchProc(HWINEVENTHOOK, DWORD event, HWND window, LONG, LONG, DWORD, DWORD) {
	if (event != EVENT_SYSTEM_MENUPOPUPSTART || !menuWatchOwner || !isStandardMenu(window)) return;
	menuWatchOwner->pullMenuToAnchor(window);
}
} // namespace

WindowsTrayItem::WindowsTrayItem(QString key, HWND window, UINT id, GUID guid, bool usesGuid, QObject* parent)
    : QObject(parent)
    , mHost(nullptr)
    , mKey(std::move(key))
    , mWindow(window)
    , mId(id)
    , mGuid(guid)
    , mUsesGuid(usesGuid) {}

bool WindowsTrayItem::activate(bool secondary, bool doubleClick) {
	return this->mHost != nullptr && this->mHost->dispatch(this, secondary, doubleClick);
}

WindowsTrayHost::WindowsTrayHost(QObject* parent): QObject(parent) {
	connect(&cleanup, &QTimer::timeout, this, &WindowsTrayHost::prune);
	cleanup.setInterval(3000);
	if (!menuWatchOwner) {
		menuWatchOwner = this;
		menuWatch = SetWinEventHook(
		    EVENT_SYSTEM_MENUPOPUPSTART,
		    EVENT_SYSTEM_MENUPOPUPSTART,
		    nullptr,
		    &menuWatchProc,
		    0,
		    0,
		    WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNTHREAD
		);
	}
}

WindowsTrayHost::~WindowsTrayHost() {
	if (menuWatch) {
		UnhookWinEvent(menuWatch);
		menuWatch = nullptr;
	}
	if (menuWatchOwner == this) menuWatchOwner = nullptr;
	if (notifyWindow) DestroyWindow(notifyWindow);
	const auto tray = trayWindow;
	// Detach first, so the destruction does not re-enter the host's own
	// bookkeeping while the QObject is being torn down.
	trayWindow = nullptr;
	if (tray) DestroyWindow(tray);
	UnregisterClassW(NotifyClass, GetModuleHandleW(nullptr));
	UnregisterClassW(TrayClass, GetModuleHandleW(nullptr));
}

WindowsTrayHost* WindowsTrayHost::instance() {
	// The receiver window and its registered icons must outlive QML reloads,
	// so exactly one host exists for the process lifetime.
	static auto* instance = new WindowsTrayHost(qGuiApp); // NOLINT
	return instance;
}

bool WindowsTrayHost::start() {
	// The receiver can disappear without us asking: windowProc forwards
	// everything except WM_COPYDATA to DefWindowProc, which destroys a window
	// on WM_CLOSE. Validate the handle instead of trusting the cached one.
	if (IsWindow(trayWindow)) return true;
	if (trayWindow) this->loseTrayWindow();
	if (FindWindowW(TrayClass, nullptr)) {
		error = QStringLiteral(
		    "Another shell owns the Windows tray. Start this shell after that tray host has exited."
		);
		emit statusChanged();
		return false;
	}
	const auto instance = GetModuleHandleW(nullptr);
	WNDCLASSW cls {};
	cls.hInstance = instance;
	cls.lpfnWndProc = &WindowsTrayHost::windowProc;
	cls.lpszClassName = TrayClass;
	if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
		error = QStringLiteral("Tray window class registration failed (%1).").arg(GetLastError());
		emit statusChanged();
		return false;
	}
	trayWindow = CreateWindowExW(
	    WS_EX_TOOLWINDOW,
	    TrayClass,
	    L"Quickshell Tray Host",
	    WS_POPUP,
	    0,
	    0,
	    0,
	    0,
	    nullptr,
	    nullptr,
	    instance,
	    this
	);
	if (!trayWindow) {
		error = QStringLiteral("Tray receiver creation failed (%1).").arg(GetLastError());
		emit statusChanged();
		return false;
	}
	cls.lpszClassName = NotifyClass;
	RegisterClassW(&cls);
	notifyWindow = CreateWindowExW(
	    0,
	    NotifyClass,
	    L"",
	    WS_CHILD,
	    0,
	    0,
	    0,
	    0,
	    trayWindow,
	    nullptr,
	    instance,
	    this
	);
	error.clear();
	cleanup.start();
	emit statusChanged();
	// Existing applications that handle this message re-register their icons.
	const auto created = RegisterWindowMessageW(L"TaskbarCreated");
	if (created) SendNotifyMessageW(HWND_BROADCAST, created, 0, 0);
	return true;
}

LRESULT CALLBACK
WindowsTrayHost::windowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
	if (message == WM_NCCREATE) {
		const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lparam);
		SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(creation->lpCreateParams));
	}
	auto* host = reinterpret_cast<WindowsTrayHost*>(GetWindowLongPtrW(window, GWLP_USERDATA));
	// Watch for the receiver's own destruction rather than trying to predict
	// what can destroy it, and drop the icons that can no longer update.
	if (host && window == host->trayWindow && message == WM_DESTROY) {
		host->loseTrayWindow();
		return 0;
	}
	if (host && message == WM_COPYDATA && lparam) {
		TrayPacket packet {};
		if (!copyTrayPacket(reinterpret_cast<const COPYDATASTRUCT*>(lparam), &packet)) return FALSE;
		return host->receive(packet.operation, packet.icon) ? TRUE : FALSE;
	}
	return DefWindowProcW(window, message, wparam, lparam);
}

WindowsTrayItem* WindowsTrayHost::find(const QString& key) const {
	for (auto* item: this->mItems.valueList()) {
		if (item->key() == key) return item;
	}
	return nullptr;
}

bool WindowsTrayHost::receive(DWORD operation, const TrayWireIcon& packet) {
	const auto window = reinterpret_cast<HWND>(static_cast<UINT_PTR>(packet.window));
	const bool useGuid = (packet.flags & NIF_GUID) != 0;
	WindowsTrayItem* item = nullptr;
	for (auto* candidate: this->mItems.valueList()) {
		const bool matches =
		    (useGuid && candidate->mUsesGuid && IsEqualGUID(candidate->mGuid, packet.guid))
		    || (!useGuid && candidate->mWindow == window && candidate->mId == packet.id);
		if (matches) {
			item = candidate;
			break;
		}
	}
	if (operation == NIM_DELETE) {
		if (!item) return false;
		this->mItems.removeObject(item);
		item->deleteLater();
		return true;
	}
	if (operation == NIM_SETFOCUS) return item != nullptr;
	if (operation == NIM_SETVERSION) {
		if (!item || packet.version > NOTIFYICON_VERSION_4) return false;
		item->mVersion = packet.version;
		return true;
	}
	if (operation != NIM_ADD && operation != NIM_MODIFY) return false;
	if (!IsWindow(window)) return false;
	if (!item) {
		if (operation != NIM_ADD) return false;
		auto key = QString::number(packet.window) + QLatin1Char(':') + QString::number(packet.id);
		if (useGuid)
			key += QString::fromLatin1(
			    QByteArray(reinterpret_cast<const char*>(&packet.guid), sizeof(GUID)).toHex()
			);
		item = new WindowsTrayItem(key, window, packet.id, packet.guid, useGuid, this);
		item->mHost = this;
		this->mItems.insertObject(item);
	}
	if (packet.flags & NIF_MESSAGE) item->mCallback = packet.callback;
	if (packet.flags & NIF_TIP) item->bindableTooltip().setValue(boundedText(packet.tip, 128));
	if (packet.flags & NIF_STATE)
		item->bindableHidden().setValue((packet.state & NIS_HIDDEN) != 0);
	if (packet.flags & NIF_ICON) {
		const auto handle = reinterpret_cast<HICON>(static_cast<UINT_PTR>(packet.icon));
		const auto duplicate = handle ? CopyIcon(handle) : nullptr;
		const auto image = duplicate ? QImage::fromHICON(duplicate) : QImage();
		if (duplicate) DestroyIcon(duplicate);
		QByteArray bytes;
		QBuffer buffer(&bytes);
		buffer.open(QIODevice::WriteOnly);
		if (!image.isNull() && image.save(&buffer, "PNG"))
			item->bindableIconSource().setValue(
			    QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64())
			);
		else item->bindableIconSource().setValue(QString());
	}
	return true;
}

bool WindowsTrayHost::dispatch(WindowsTrayItem* item, bool secondary, bool doubleClick) {
	if (!item || !item->mCallback || !IsWindow(item->mWindow)) return false;
	POINT cursor {};
	GetCursorPos(&cursor);
	// Remember the anchor so a menu that opens somewhere unexpected can
	// be pulled back to where the user actually clicked.
	anchor = cursor;
	anchorTick = GetTickCount();
	DWORD process = 0;
	GetWindowThreadProcessId(item->mWindow, &process);
	AllowSetForegroundWindow(process);
	SetForegroundWindow(item->mWindow);
	auto send = [&](UINT event) {
		const WPARAM wparam = item->mVersion == NOTIFYICON_VERSION_4
		                        ? static_cast<WPARAM>(MAKELONG(cursor.x, cursor.y))
		                        : item->mId;
		const LPARAM lparam =
		    item->mVersion == NOTIFYICON_VERSION_4 ? MAKELONG(event, item->mId) : event;
		return PostMessageW(item->mWindow, item->mCallback, wparam, lparam) != 0;
	};
	if (item->mVersion == NOTIFYICON_VERSION_4)
		return send(secondary ? WM_CONTEXTMENU : NIN_SELECT);
	if (doubleClick) return send(secondary ? WM_RBUTTONDBLCLK : WM_LBUTTONDBLCLK);
	const bool down = send(secondary ? WM_RBUTTONDOWN : WM_LBUTTONDOWN);
	return send(secondary ? WM_RBUTTONUP : WM_LBUTTONUP) && down;
}

void WindowsTrayHost::pullMenuToAnchor(HWND menu) {
	if (GetTickCount() - anchorTick > MenuRepositionWindow) return;
	RECT bounds {};
	if (!GetWindowRect(menu, &bounds)) return;
	const auto dx = bounds.left - anchor.x;
	const auto dy = bounds.top - anchor.y;
	// Applications nudge their own menus a few pixels; only correct a landing
	// far enough away to read as a mistake.
	if (abs(dx) <= MenuRepositionTolerance && abs(dy) <= MenuRepositionTolerance) return;
	SetWindowPos(menu, nullptr, anchor.x, anchor.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void WindowsTrayHost::loseTrayWindow() {
	trayWindow = nullptr;
	notifyWindow = nullptr;
	cleanup.stop();
	error = QStringLiteral(
	    "The Windows tray receiver was destroyed. Retry the tray host to register icons again."
	);
	// These icons were registered with a receiver that no longer exists, so
	// they can never update or accept new registrations. Leaving them in the
	// model makes an empty tray look like a working one.
	const auto stale = this->mItems.valueList();
	if (!stale.isEmpty()) {
		this->mItems.diffUpdate({});
		for (auto* item: stale) item->deleteLater();
	}
	emit statusChanged();
}

void WindowsTrayHost::prune() {
	const auto items = this->mItems.valueList();
	for (qsizetype row = items.length() - 1; row >= 0; --row) {
		auto* item = items.at(row);
		if (item->alive()) continue;
		this->mItems.removeAt(row);
		item->deleteLater();
	}
}
