#include "launcher_shortcut.hpp"

#include <qcursor.h>
#include <qguiapplication.h>
#include <qscreen.h>

namespace {
constexpr int ShortcutId = 1; // Scoped to our private HWND.
}

WindowsLauncherShortcut* WindowsLauncherShortcut::hookOwner = nullptr;
LRESULT CALLBACK WindowsLauncherShortcut::keyboardProc(int code, WPARAM message, LPARAM data) {
	if (code == HC_ACTION && hookOwner) {
		const auto* key = reinterpret_cast<KBDLLHOOKSTRUCT*>(data);
		if (key->vkCode == 'W' && !(key->flags & LLKHF_INJECTED)) {
			const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
			const bool win = (GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000);
			const bool otherModifier = (GetAsyncKeyState(VK_CONTROL) & 0x8000)
			    || (GetAsyncKeyState(VK_MENU) & 0x8000) || (GetAsyncKeyState(VK_SHIFT) & 0x8000);
			if (down && (hookOwner->closePressed || (win && !otherModifier))) {
				if (!hookOwner->closePressed) {
					hookOwner->closePressed = true;
					// Keep the hook callback short; execute the close on our event loop.
					PostMessageW(hookOwner->handle, WM_HOTKEY, 2, 0);
				}
				return 1;
			}
			if (!down && hookOwner->closePressed) {
				hookOwner->closePressed = false;
				return 1;
			}
		}
	}
	return CallNextHookEx(nullptr, code, message, data);
}

WindowsLauncherShortcut::WindowsLauncherShortcut(QObject* parent): QObject(parent) {
	receiver.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
	receiver.setTitle("Quickshell Launcher Shortcut");
	handle = reinterpret_cast<HWND>(receiver.winId());
	qGuiApp->installNativeEventFilter(this);
	closeRegistered = RegisterHotKey(handle, 2, MOD_WIN | MOD_NOREPEAT, 'W');
	if (closeRegistered) closeCombination = QStringLiteral("Win+W");
	else {
		// Windows reserves Win+W for Widgets. Intercept only this combination
		// while our shell is running; leave all other keyboard input untouched.
		hookOwner = this;
		closeHook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardProc, GetModuleHandleW(nullptr), 0);
		if (closeHook) closeCombination = QStringLiteral("Win+W");
		else {
			hookOwner = nullptr;
			closeRegistered = RegisterHotKey(handle, 2, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'W');
			if (closeRegistered) closeCombination = QStringLiteral("Ctrl+Alt+W");
		}
	}
	registered = RegisterHotKey(handle, ShortcutId, MOD_WIN | MOD_NOREPEAT, VK_SPACE);
	if (registered) {
		combination = QStringLiteral("Win+Space");
	} else {
		const auto preferredError = GetLastError();
		registered = RegisterHotKey(handle, ShortcutId, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_SPACE);
		if (registered) {
			combination = QStringLiteral("Ctrl+Alt+Space");
			error = QStringLiteral("Win+Space is unavailable (Windows error %1). Using Ctrl+Alt+Space.")
			            .arg(preferredError);
		} else {
			const auto fallbackError = GetLastError();
			error = QStringLiteral(
			            "Launcher shortcuts are unavailable: Win+Space (%1), Ctrl+Alt+Space (%2). Use "
			            "the bar button."
			)
			            .arg(preferredError)
			            .arg(fallbackError);
		}
	}
}

WindowsLauncherShortcut* WindowsLauncherShortcut::instance() {
	// A new QML engine starts before the old one is destroyed during reload.
	// Keep one hotkey registration for the entire application lifetime.
	static auto* instance = new WindowsLauncherShortcut(qGuiApp); // NOLINT
	return instance;
}

WindowsLauncherShortcut::~WindowsLauncherShortcut() {
	if (closeHook) UnhookWindowsHookEx(closeHook);
	if (hookOwner == this) hookOwner = nullptr;
	if (closeRegistered) UnregisterHotKey(handle, 2);
	if (registered) UnregisterHotKey(handle, ShortcutId);
	qGuiApp->removeNativeEventFilter(this);
	receiver.destroy();
}

bool WindowsLauncherShortcut::nativeEventFilter(
    const QByteArray& type,
    void* message,
    qintptr* result
) {
	if (type != "windows_generic_MSG" && type != "windows_dispatcher_MSG") return false;
	const auto* msg = static_cast<MSG*>(message);
	if ((closeRegistered || closeHook) && msg->hwnd == handle && msg->message == WM_HOTKEY && msg->wParam == 2) {
		emit closeActivated();
		if (result) *result = 0;
		return true;
	}
	if (!registered || msg->hwnd != handle || msg->message != WM_HOTKEY || msg->wParam != ShortcutId)
		return false;
	const auto* screen = QGuiApplication::screenAt(QCursor::pos());
	emit activated(screen ? screen->name() : QString());
	if (result) *result = 0;
	return true;
}
