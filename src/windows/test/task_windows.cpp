#include "../task_windows.hpp"
#include "../launcher_shortcut.hpp"
#include "../settings_window.hpp"
#include <qimage.h>
#include <qguiapplication.h>
#include <qprocess.h>
#include <qscopeguard.h>
#include <qtest.h>

namespace {
bool allowClose = false;
int liveWindows = 0;
LRESULT CALLBACK testProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_CREATE) ++liveWindows;
  if (message == WM_ENTERMENULOOP) SetWindowTextW(window, L"Quickshell task test native menu opened");
  if (message == WM_APP) { allowClose = true; return 0; }
  if (message == WM_CLOSE) {
   SetWindowTextW(window, L"Quickshell task test close requested");
   if (allowClose) DestroyWindow(window);
   return 0;
  }
  if (message == WM_DESTROY) { if (--liveWindows == 0) PostQuitMessage(0); return 0; }
  return DefWindowProcW(window, message, wparam, lparam);
}
}
class TaskTest: public QObject {
	Q_OBJECT
private slots:
	void lateWindowIcon() {
		WNDCLASSW cls {};
		cls.lpfnWndProc = DefWindowProcW;
		cls.hInstance = GetModuleHandleW(nullptr);
		cls.lpszClassName = L"QuickshellLateIconTest";
		QVERIFY(RegisterClassW(&cls));
		auto window = CreateWindowExW(
		    0,
		    cls.lpszClassName,
		    L"Late icon",
		    WS_OVERLAPPEDWINDOW,
		    0,
		    0,
		    240,
		    160,
		    nullptr,
		    nullptr,
		    cls.hInstance,
		    nullptr
		);
		auto cleanup = qScopeGuard([&] {
			DestroyWindow(window);
			UnregisterClassW(cls.lpszClassName, cls.hInstance);
		});
		QVERIFY(window);
		WindowsTaskWindow item(window);
		item.refresh();
		const auto initialIcon = item.iconSource();
		QVERIFY(!initialIcon.isEmpty());
		SendMessageW(
		    window,
		    WM_SETICON,
		    ICON_SMALL,
		    reinterpret_cast<LPARAM>(LoadIconW(nullptr, IDI_WARNING))
		);
		item.refresh();
		QVERIFY(item.iconSource() != initialIcon);
		const auto updatedIcon = item.iconSource();
		item.refresh();
		QCOMPARE(item.iconSource(), updatedIcon);
		// A Settings frame may expose a generic native icon even after WM_SETICON.
		IPropertyStore* store = nullptr;
		QVERIFY(SUCCEEDED(SHGetPropertyStoreForWindow(window, IID_PPV_ARGS(&store))));
		auto releaseStore = qScopeGuard([&] { store->Release(); });
		PROPVARIANT identity {};
		identity.vt = VT_LPWSTR;
		identity.pwszVal = const_cast<wchar_t*>(qs::windows::settings::appId);
		QVERIFY(SUCCEEDED(store->SetValue(PKEY_AppUserModel_ID, identity)));
		QVERIFY(SUCCEEDED(store->Commit()));
		item.refresh();
		const auto encoded = item.iconSource().mid(QStringLiteral("data:image/png;base64,").length());
		const auto actual = QImage::fromData(QByteArray::fromBase64(encoded.toLatin1()));
		const QImage expected(QString::fromStdWString(qs::windows::settings::iconPath().wstring()));
		QVERIFY(!expected.isNull());
		QCOMPARE(
		    actual.convertToFormat(QImage::Format_ARGB32),
		    expected.convertToFormat(QImage::Format_ARGB32)
		);
		// Centering uses the monitor work area and preserves the frame size.
		const auto monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
		MONITORINFO monitorInfo {};
		monitorInfo.cbSize = sizeof(monitorInfo);
		QVERIFY(GetMonitorInfoW(monitor, &monitorInfo));
		RECT before {}, after {};
		QVERIFY(GetWindowRect(window, &before));
		QVERIFY(qs::windows::settings::centerFrame(window, monitor));
		QVERIFY(GetWindowRect(window, &after));
		QCOMPARE(after.right - after.left, before.right - before.left);
		QCOMPARE(after.bottom - after.top, before.bottom - before.top);
		QCOMPARE(
		    after.left,
		    monitorInfo.rcWork.left
		        + (monitorInfo.rcWork.right - monitorInfo.rcWork.left - (before.right - before.left))
		              / 2
		);
		QCOMPARE(
		    after.top,
		    monitorInfo.rcWork.top
		        + (monitorInfo.rcWork.bottom - monitorInfo.rcWork.top - (before.bottom - before.top))
		              / 2
		);
	}
	void grouping() {
		QProcess child;
		child.start(QCoreApplication::applicationFilePath(), {"--window-child", "--group-child"});
		QVERIFY(child.waitForStarted());
		auto cleanup = qScopeGuard([&] { if (child.state() != QProcess::NotRunning) { child.kill(); child.waitForFinished(); } });
		HWND first = nullptr;
		HWND second = nullptr;
		QTRY_VERIFY_WITH_TIMEOUT((first = FindWindowW(L"QuickshellTaskTest", L"Quickshell task test")), 10000);
		QTRY_VERIFY_WITH_TIMEOUT((second = FindWindowW(L"QuickshellTaskTest", L"Quickshell task test second")), 10000);
		WindowsTaskWindows tasks;
		WindowsTaskGroup* group = nullptr;
		for (auto* candidate: tasks.groups()->valueList())
			if (candidate->process() == static_cast<DWORD>(child.processId())) group = candidate;
		QVERIFY(group);
		QCOMPARE(group->windowCount(), 2);
		auto order = group->windows()->valueList();
		QCOMPARE(order.length(), 2);
		QTRY_VERIFY_WITH_TIMEOUT((order.first()->activate(), GetForegroundWindow() == order.first()->window()), 10000);
		group->activate();
		QTRY_COMPARE_WITH_TIMEOUT(GetForegroundWindow(), order.last()->window(), 10000);
		group->activate();
		QTRY_COMPARE_WITH_TIMEOUT(GetForegroundWindow(), order.first()->window(), 10000);
		tasks.refresh();
		QCOMPARE(group->windows()->valueList(), order);
		QVERIFY(group->active());
		PostMessageW(second, WM_APP, 0, 0);
		PostMessageW(second, WM_CLOSE, 0, 0);
		QTRY_VERIFY_WITH_TIMEOUT(!IsWindow(second), 10000);
		tasks.refresh();
		QCOMPARE(group->windowCount(), 1);
		QCOMPARE(group->windows()->valueList().first()->window(), first);
		group->requestCloseAll();
		QTRY_VERIFY_WITH_TIMEOUT(!IsWindow(first), 10000);
		tasks.refresh();
		QVERIFY(!tasks.groups()->valueList().contains(group));
	}
	void lifecycle() {
		QProcess child;
		child.start(QCoreApplication::applicationFilePath(), {"--window-child"});
		QVERIFY(child.waitForStarted());
		HWND window = nullptr;
		QTRY_VERIFY_WITH_TIMEOUT((window = FindWindowW(L"QuickshellTaskTest", nullptr)) != nullptr, 5000);
		WindowsTaskWindows tasks;
		QVERIFY(WindowsTaskWindows::eligible(window));
		auto find = [&]() -> WindowsTaskWindow* {
			tasks.refresh();
			for (auto* item: tasks.windows()->valueList())
				if (item->window() == window) return item;
			return nullptr;
		};
		auto* item = find();
		QVERIFY(item);
		QVERIFY(item->iconSource().startsWith("data:image/png;base64,"));
		// SetForegroundWindow is refused while another process owns the foreground,
		// so keep asking instead of asserting that the first attempt took.
		QTRY_VERIFY_WITH_TIMEOUT((item->activate(), GetForegroundWindow() == window), 5000);
		item->activate();
		QTRY_VERIFY_WITH_TIMEOUT(IsIconic(window), 5000);
		item->showContextMenu();
		QTRY_VERIFY_WITH_TIMEOUT(
		    (item = find(), item && item->title().contains("native menu opened")),
		    5000
		);
		PostMessageW(window, WM_CANCELMODE, 0, 0);
		QVERIFY(!WindowsTaskWindows::requestClose(GetShellWindow()));
		QVERIFY(!WindowsTaskWindows::requestClose(nullptr));
		QVERIFY(WindowsTaskWindows::requestClose(window));
		QTRY_VERIFY_WITH_TIMEOUT(
		    (item = find(), item && item->title().contains("close requested")),
		    5000
		);
		QVERIFY(IsWindow(window)); // Application veto/save prompt is respected.
		PostMessageW(window, WM_APP, 0, 0);
		QVERIFY(WindowsTaskWindows::requestClose(window));
		QTRY_VERIFY_WITH_TIMEOUT(!IsWindow(window), 5000);
		QCOMPARE(find(), nullptr);
		QTRY_COMPARE_WITH_TIMEOUT(child.state(), QProcess::NotRunning, 5000);
	}
	void shortcutRegistration() {
		WindowsLauncherShortcut shortcuts;
		qInfo() << "Close binding:" << shortcuts.closeShortcutText();
		QVERIFY(
		    shortcuts.closeShortcutText().isEmpty()
		    || shortcuts.closeShortcutText() == "Win+W"
		    || shortcuts.closeShortcutText() == "Ctrl+Alt+W"
		);
	}
};
int main(int argc, char** argv) {
 QGuiApplication app(argc, argv);
 if (app.arguments().contains("--window-child")) {
  WNDCLASSW cls {};
  cls.lpfnWndProc = testProc;
  cls.hInstance = GetModuleHandleW(nullptr);
  cls.lpszClassName = L"QuickshellTaskTest";
  RegisterClassW(&cls);
  CreateWindowExW(WS_EX_APPWINDOW, cls.lpszClassName, L"Quickshell task test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                 0, 0, 240, 160, nullptr, nullptr, cls.hInstance, nullptr);
  MSG message {};
  if (app.arguments().contains("--group-child"))
   CreateWindowExW(WS_EX_APPWINDOW, cls.lpszClassName, L"Quickshell task test second", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                  260, 0, 240, 160, nullptr, nullptr, cls.hInstance, nullptr);
  while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
  return 0;
 }
 TaskTest test;
 return QTest::qExec(&test, argc, argv);
}
#include "task_windows.moc"
