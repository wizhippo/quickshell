#include <QtTest/qtest.h>
#include "../tray_host.hpp"
#include <qabstractnativeeventfilter.h>
#include <qguiapplication.h>
#include <qwindow.h>
#include <shellapi.h>


class CallbackCollector: public QAbstractNativeEventFilter {
public:
	HWND window = nullptr;
	QVector<MSG> events;
	bool nativeEventFilter(const QByteArray&, void* message, qintptr*) override {
		const auto* event = static_cast<MSG*>(message);
		if (event->hwnd == window && event->message == WM_APP + 17) events.append(*event);
		return false;
	}
};

class TrayHostTest: public QObject {
	Q_OBJECT;
private slots:
	void unreadablePacket() {
		if (FindWindowW(L"Shell_TrayWnd", nullptr))
			QSKIP("Another shell already owns the tray; do not interrupt it.");
		WindowsTrayHost host;
		QVERIFY(host.start());
		const auto receiver = FindWindowW(L"Shell_TrayWnd", nullptr);
		auto* memory = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
		QVERIFY(memory);
		struct FreeMemory {
			void* memory;
			~FreeMemory() { VirtualFree(memory, 0, MEM_RELEASE); }
		} guard {memory};
		COPYDATASTRUCT packet {};
		packet.dwData = 1;
		packet.cbData = 4096;
		packet.lpData = memory;
		// Same-thread SendMessage passes these pointers directly to the receiver.
		QCOMPARE(SendMessageW(receiver, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&packet)), LRESULT(0));
		QCOMPARE(SendMessageW(receiver, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(memory)), LRESULT(0));
		QCOMPARE(host.items()->rowCount({}), 0);
		QVERIFY(host.available());
	}
	void notificationLifecycle() {
		if (FindWindowW(L"Shell_TrayWnd", nullptr))
			QSKIP("Another shell already owns the tray; do not interrupt it.");
		QWindow sender;
		const auto owner = reinterpret_cast<HWND>(sender.winId());
		CallbackCollector collector;
		collector.window = owner;
		qGuiApp->installNativeEventFilter(&collector);
		struct RemoveFilter {
			CallbackCollector* filter;
			~RemoveFilter() { qGuiApp->removeNativeEventFilter(filter); }
		} filterGuard {&collector};
		{
			WindowsTrayHost host;
			QVERIFY(host.start());
			QVERIFY(host.start()); // Starting twice retains the same receiver.
			NOTIFYICONDATAW icon {};
			icon.cbSize = sizeof(icon);
			icon.hWnd = owner;
			icon.uID = 71;
			icon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
			icon.uCallbackMessage = WM_APP + 17;
			icon.hIcon = LoadIconW(nullptr, IDI_INFORMATION);
			wcscpy(icon.szTip, L"Quickshell native tray test");
			QVERIFY(Shell_NotifyIconW(NIM_ADD, &icon));
			auto findItem = [&] {
				for (auto* item: host.items()->valueList())
					if (item->tooltip().startsWith("Quickshell native tray test")) return item;
				return static_cast<WindowsTrayItem*>(nullptr);
			};
			QVERIFY(findItem());
			auto* item = findItem();
			QVERIFY(item->iconSource().startsWith("data:image/png;base64,"));
			icon.uVersion = NOTIFYICON_VERSION_4;
			QVERIFY(Shell_NotifyIconW(NIM_SETVERSION, &icon));
			QVERIFY(item->activate());
			QTRY_VERIFY(!collector.events.isEmpty());
			QCOMPARE(LOWORD(collector.events.last().lParam), NIN_SELECT);
			QCOMPARE(HIWORD(collector.events.last().lParam), 71);
			collector.events.clear();
			QVERIFY(item->activate(true));
			QTRY_VERIFY(!collector.events.isEmpty());
			QCOMPARE(LOWORD(collector.events.last().lParam), WM_CONTEXTMENU);
			// Version 4 callbacks carry the cursor anchor in wParam, which is
			// what the application positions its menu from.
			collector.events.clear();
			const POINT anchor {1700, 640};
			QVERIFY(SetCursorPos(anchor.x, anchor.y));
			QVERIFY(item->activate(true));
			QTRY_VERIFY(!collector.events.isEmpty());
			QCOMPARE(int(LOWORD(collector.events.last().wParam)), anchor.x);
			QCOMPARE(int(HIWORD(collector.events.last().wParam)), anchor.y);
			icon.uFlags = NIF_TIP | NIF_STATE;
			wcscpy(icon.szTip, L"Quickshell native tray test updated");
			icon.dwState = NIS_HIDDEN;
			icon.dwStateMask = NIS_HIDDEN;
			QVERIFY(Shell_NotifyIconW(NIM_MODIFY, &icon));
			QVERIFY(item->hidden());
			// The same registration updates the existing item rather than
			// adding a second one.
			QCOMPARE(host.items()->rowCount({}), 1);
			COPYDATASTRUCT malformed {};
			malformed.dwData = 1;
			DWORD tiny = 0;
			malformed.cbData = sizeof(tiny);
			malformed.lpData = &tiny;
			QCOMPARE(
			    SendMessageW(
			        FindWindowW(L"Shell_TrayWnd", nullptr),
			        WM_COPYDATA,
			        0,
			        reinterpret_cast<LPARAM>(&malformed)
			    ),
			    LRESULT(0)
			);
			QVERIFY(Shell_NotifyIconW(NIM_DELETE, &icon));
			QCOMPARE(findItem(), nullptr);
			QVERIFY(!item->activate());
		}
		QVERIFY(!FindWindowW(L"Shell_TrayWnd", nullptr));
		WindowsTrayHost next;
		QVERIFY(next.start()); // Shutdown released the class and native receiver.
	}
	void receiverLoss() {
		if (FindWindowW(L"Shell_TrayWnd", nullptr))
			QSKIP("Another shell already owns the tray; do not interrupt it.");
		QWindow sender;
		const auto owner = reinterpret_cast<HWND>(sender.winId());
		WindowsTrayHost host;
		QVERIFY(host.start());
		NOTIFYICONDATAW icon {};
		icon.cbSize = sizeof(icon);
		icon.hWnd = owner;
		icon.uID = 91;
		icon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
		icon.uCallbackMessage = WM_APP + 19;
		icon.hIcon = LoadIconW(nullptr, IDI_INFORMATION);
		wcscpy(icon.szTip, L"Quickshell tray receiver loss test");
		QVERIFY(Shell_NotifyIconW(NIM_ADD, &icon));
		QTRY_COMPARE(host.items()->rowCount({}), 1);
		const auto key = host.items()->valueList().first()->key();

		// A stray WM_CLOSE is handled by DefWindowProc, which destroys the
		// receiver without the host ever being asked to release it.
		const auto receiver = FindWindowW(L"Shell_TrayWnd", nullptr);
		QVERIFY(receiver);
		SendMessageW(receiver, WM_CLOSE, 0, 0);
		QVERIFY(!host.available());
		QVERIFY(!FindWindowW(L"Shell_TrayWnd", nullptr));
		QCOMPARE(host.items()->rowCount({}), 0);
		QVERIFY(!host.errorString().isEmpty());
		QVERIFY(!host.find(key));

		QVERIFY(host.start()); // Retrying rebuilds the receiver and re-registers.
		QVERIFY(host.available());
		QVERIFY(host.errorString().isEmpty());
		QVERIFY(Shell_NotifyIconW(NIM_ADD, &icon));
		QTRY_COMPARE(host.items()->rowCount({}), 1);
		QVERIFY(Shell_NotifyIconW(NIM_DELETE, &icon));
		QCOMPARE(host.items()->rowCount({}), 0);
	}
	void menuCorrection() {
		if (FindWindowW(L"Shell_TrayWnd", nullptr))
			QSKIP("Another shell already owns the tray; do not interrupt it.");
		QWindow sender;
		const auto owner = reinterpret_cast<HWND>(sender.winId());
		WindowsTrayHost host;
		QVERIFY(host.start());
		NOTIFYICONDATAW icon {};
		icon.cbSize = sizeof(icon);
		icon.hWnd = owner;
		icon.uID = 92;
		icon.uFlags = NIF_MESSAGE | NIF_TIP;
		icon.uCallbackMessage = WM_APP + 21;
		wcscpy(icon.szTip, L"Quickshell tray menu correction test");
		QVERIFY(Shell_NotifyIconW(NIM_ADD, &icon));
		icon.uVersion = NOTIFYICON_VERSION_4;
		QVERIFY(Shell_NotifyIconW(NIM_SETVERSION, &icon));
		QTRY_COMPARE(host.items()->rowCount({}), 1);
		auto* item = host.items()->valueList().first();

		WNDCLASSW cls {};
		cls.hInstance = GetModuleHandleW(nullptr);
		cls.lpfnWndProc = DefWindowProcW;
		cls.lpszClassName = L"QuickshellMenuCorrectionWindow";
		RegisterClassW(&cls);
		const auto create = [&](int x, int y) {
			return CreateWindowExW(
			    0, cls.lpszClassName, L"", WS_POPUP, x, y, 120, 90,
			    nullptr, nullptr, cls.hInstance, nullptr
			);
		};

		// An application that opened its menu across the screen has it moved
		// back to where the user clicked.
		const POINT anchor {1500, 500};
		QVERIFY(SetCursorPos(anchor.x, anchor.y));
		QVERIFY(item->activate(true));
		const auto stray = create(40, 30);
		QVERIFY(stray);
		host.pullMenuToAnchor(stray);
		RECT moved {};
		QVERIFY(GetWindowRect(stray, &moved));
		QCOMPARE(moved.left, anchor.x);
		QCOMPARE(moved.top, anchor.y);

		// One the application already placed under the pointer is left alone.
		QVERIFY(SetCursorPos(anchor.x + 6, anchor.y + 5));
		QVERIFY(item->activate(true));
		const auto kept = create(anchor.x + 6, anchor.y + 5);
		QVERIFY(kept);
		host.pullMenuToAnchor(kept);
		RECT untouched {};
		QVERIFY(GetWindowRect(kept, &untouched));
		QCOMPARE(untouched.left, anchor.x + 6);
		QCOMPARE(untouched.top, anchor.y + 5);

		DestroyWindow(stray);
		DestroyWindow(kept);
		UnregisterClassW(cls.lpszClassName, cls.hInstance);
		QVERIFY(Shell_NotifyIconW(NIM_DELETE, &icon));
		QCOMPARE(host.items()->rowCount({}), 0);
	}
};
QTEST_MAIN(TrayHostTest)
#include "tray_host.moc"
