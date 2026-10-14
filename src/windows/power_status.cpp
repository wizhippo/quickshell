#include "power_status.hpp"
#include <QGuiApplication>
#include <QLoggingCategory>

namespace {
// Microsoft Power Setting GUIDs, kept local so the backend does not depend on
// which SDK names happen to be exported by the uuid library.
constexpr GUID AcPowerSourceGuid{0x5d3e9a59, 0xe9d5, 0x4b00, {0xa6, 0xbd, 0xff, 0x34, 0xff, 0x51, 0x65, 0x48}};
constexpr GUID BatteryPercentageGuid{0xa7ad8041, 0xb45a, 0x4cae, {0x87, 0xa3, 0xee, 0xcb, 0xb4, 0x68, 0xa9, 0xe1}};
constexpr GUID PowerSavingStatusGuid{0xe00958c0, 0xc213, 0x4ace, {0xac, 0x77, 0xfe, 0xcc, 0xed, 0x2e, 0xee, 0xa5}};
}

PowerStatus::PowerStatus(QObject* parent): QObject(parent) {
	// A hidden top-level window receives broadcasts independently of panels.
	// It reserves no workspace and has no taskbar entry.
	notificationWindow.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
	notificationWindow.setTitle("Quickshell Power Notifications");
	notificationHandle = reinterpret_cast<HWND>(notificationWindow.winId());
	qGuiApp->installNativeEventFilter(this);
	for (const auto* guid: {&AcPowerSourceGuid, &BatteryPercentageGuid, &PowerSavingStatusGuid}) {
		const auto registration = RegisterPowerSettingNotification(notificationHandle, guid, DEVICE_NOTIFY_WINDOW_HANDLE);
		if (registration) registrations.push_back(registration);
		else qWarning() << "Power notification registration failed; periodic refresh remains enabled:" << GetLastError();
	}
	connect(&timer, &QTimer::timeout, this, &PowerStatus::refresh);
	timer.setInterval(30000);
	timer.setTimerType(Qt::CoarseTimer);
	timer.start();
	refresh();
}

PowerStatus::~PowerStatus() {
	for (const auto registration: registrations) UnregisterPowerSettingNotification(registration);
	qGuiApp->removeNativeEventFilter(this);
	notificationWindow.destroy();
}

void PowerStatus::refresh() {
	SYSTEM_POWER_STATUS native{};
	qs::windows::PowerReading updated;
	QString error;
	if (GetSystemPowerStatus(&native)) updated = qs::windows::decodePowerStatus(native);
	else error = QStringLiteral("GetSystemPowerStatus failed (Windows error %1).").arg(GetLastError());
	if (updated == reading && error == lastError) return;
	// Replace the entire snapshot before notifying QML, so bindings see one
	// consistent reading rather than partially updated properties.
	reading = updated;
	lastError = error;
	emit statusChanged();
}

void PowerStatus::scheduleRefresh() {
	if (refreshPending) return;
	refreshPending = true;
	QTimer::singleShot(0, this, [this] {
		refreshPending = false;
		refresh();
	});
}

bool PowerStatus::nativeEventFilter(const QByteArray& type, void* message, qintptr*) {
	if (type != "windows_generic_MSG" && type != "windows_dispatcher_MSG") return false;
	const auto* msg = static_cast<MSG*>(message);
	if (msg->hwnd == notificationHandle && msg->message == WM_POWERBROADCAST) {
		if (msg->wParam == PBT_POWERSETTINGCHANGE || msg->wParam == PBT_APMPOWERSTATUSCHANGE
		    || msg->wParam == PBT_APMRESUMEAUTOMATIC || msg->wParam == PBT_APMRESUMESUSPEND) {
			scheduleRefresh();
		}
	}
	return false; // Qt and the default window procedure still process broadcasts.
}
