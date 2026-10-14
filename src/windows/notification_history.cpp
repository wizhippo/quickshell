// Include projections before Qt's keyword macros.
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/Windows.UI.Notifications.Management.h>
#include <roapi.h>

#include "notification_history.hpp"
#include <algorithm>
#include <chrono>
#include <qcoreapplication.h>
#include <qhash.h>
#include <qpointer.h>
#include <qscopeguard.h>
#include <qthreadpool.h>
#include <qtimezone.h>

#include "../core/logcat.hpp"

namespace {
QS_LOGGING_CATEGORY(logHistory, "quickshell.windows.notifications", QtWarningMsg);
using namespace winrt::Windows::UI::Notifications;
using namespace winrt::Windows::UI::Notifications::Management;
using Access = NotificationHistory::Access;

QString text(const winrt::hstring& value) {
	return QString::fromWCharArray(value.c_str(), static_cast<int>(value.size()));
}
Access accessValue(UserNotificationListenerAccessStatus status) {
	switch (status) {
		case UserNotificationListenerAccessStatus::Allowed: return Access::Allowed;
		case UserNotificationListenerAccessStatus::Denied: return Access::Denied;
		default: return Access::Unspecified;
	}
}
QString accessError(Access access) {
	if (access == Access::Denied)
		return QStringLiteral("Windows denied notification access. Enable access in Windows notification privacy settings.");
	if (access == Access::Unspecified)
		return QStringLiteral("Notification access has not been granted. Select Allow notification access.");
	return {};
}
QString nativeError(const winrt::hresult_error& error) {
	return QStringLiteral("Windows notification history unavailable (0x%1): %2")
	    .arg(static_cast<quint32>(error.code().value), 8, 16, QLatin1Char('0'))
	    .arg(text(error.message()));
}
struct Snapshot {
	Access access = Access::Unavailable;
	QString error;
	QList<WindowsNotificationData> data;
};
Snapshot readHistory() {
	Snapshot result;
	const auto apartment = RoInitialize(RO_INIT_MULTITHREADED);
	if (FAILED(apartment)) {
		result.error = nativeError(winrt::hresult_error(apartment));
		return result;
	}
	auto uninitialize = qScopeGuard([] { RoUninitialize(); });
	try {
		auto listener = UserNotificationListener::Current();
		result.access = accessValue(listener.GetAccessStatus());
		result.error = accessError(result.access);
		if (result.access != Access::Allowed) return result;
		auto operation = listener.GetNotificationsAsync(NotificationKinds::Toast);
		if (operation.wait_for(std::chrono::seconds(5)) == winrt::Windows::Foundation::AsyncStatus::Started) {
			operation.Cancel();
			throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_TIMEOUT));
		}
		for (const auto& notification: operation.get()) {
			WindowsNotificationData data;
			data.id = notification.Id();
			try { data.appName = text(notification.AppInfo().DisplayInfo().DisplayName()); }
			catch (const winrt::hresult_error&) { /* The sending app may have been uninstalled. */ }
			const auto timestamp = winrt::clock::to_sys(notification.CreationTime());
			data.created = QDateTime::fromMSecsSinceEpoch(
			    std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch()).count(),
			    QTimeZone::UTC
			);
			auto binding = notification.Notification().Visual().GetBinding(KnownNotificationBindings::ToastGeneric());
			if (binding) {
				QStringList lines;
				for (const auto& element: binding.GetTextElements()) lines.append(text(element.Text()));
				if (!lines.isEmpty()) data.summary = lines.takeFirst();
				data.body = lines.join(QLatin1Char('\n'));
			}
			result.data.append(std::move(data));
		}
		// Revocation silently returns an empty list. Check again before publishing.
		result.access = accessValue(listener.GetAccessStatus());
		result.error = accessError(result.access);
		if (result.access != Access::Allowed) result.data.clear();
		std::sort(result.data.begin(), result.data.end(), [](const auto& a, const auto& b) {
			return a.created == b.created ? a.id > b.id : a.created > b.created;
		});
	} catch (const winrt::hresult_error& error) {
		result.access = Access::Unavailable;
		result.error = nativeError(error);
		result.data.clear();
	}
	return result;
}
} // namespace

struct NotificationHistory::Native {
	HRESULT apartment = RoInitialize(RO_INIT_SINGLETHREADED);
	winrt::Windows::Foundation::IAsyncOperation<UserNotificationListenerAccessStatus> request {nullptr};
	~Native() {
		if (request) { try { request.Cancel(); } catch (const winrt::hresult_error&) {} }
		request = nullptr;
		if (SUCCEEDED(apartment)) RoUninitialize();
	}
};

WindowsNotification::WindowsNotification(const WindowsNotificationData& data, QObject* parent)
    : QObject(parent), mId(data.id) { this->update(data); }

void WindowsNotification::update(const WindowsNotificationData& data) {
	this->bAppName = data.appName;
	this->bSummary = data.summary;
	this->bBody = data.body;
	this->bCreated = data.created;
}

NotificationHistory::NotificationHistory(QObject* parent): QObject(parent), native(std::make_unique<Native>()) {
	this->timer.setInterval(10000);
	connect(&this->timer, &QTimer::timeout, this, &NotificationHistory::refresh);
	this->timer.start();
	this->refresh();
}
NotificationHistory::~NotificationHistory() = default;

void NotificationHistory::refresh() {
	if (this->bBusy) return;
	this->bBusy = true;
	// Only inspect the QPointer on the GUI thread. The worker owns only plain
	// snapshot data; its WinRT objects and apartment are destroyed on that worker.
	QPointer<NotificationHistory> guard(this);
	QThreadPool::globalInstance()->start([guard] {
		auto snapshot = readHistory();
		QMetaObject::invokeMethod(qApp, [guard, snapshot = std::move(snapshot)] {
			if (guard) guard->apply(snapshot.access, snapshot.error, snapshot.data);
		}, Qt::QueuedConnection);
	});
}

void NotificationHistory::requestAccess() {
	if (this->bBusy) return;
	try {
		if (FAILED(this->native->apartment) && this->native->apartment != RPC_E_CHANGED_MODE)
			throw winrt::hresult_error(this->native->apartment);
		this->bBusy = true;
		this->native->request = UserNotificationListener::Current().RequestAccessAsync();
		QPointer<NotificationHistory> guard(this);
		this->native->request.Completed([guard](const auto& operation, auto) {
			Access access = Access::Unavailable;
			QString error;
			try { access = accessValue(operation.GetResults()); error = accessError(access); }
			catch (const winrt::hresult_error& failure) { error = nativeError(failure); }
			QMetaObject::invokeMethod(qApp, [guard, access, error] {
				if (!guard) return;
				guard->native->request = nullptr;
				guard->apply(access, error, {});
				if (access == Access::Allowed) guard->refresh();
			}, Qt::QueuedConnection);
		});
	} catch (const winrt::hresult_error& error) {
		this->apply(Access::Unavailable, nativeError(error), {});
	}
}

void NotificationHistory::apply(Access access, const QString& error, const QList<WindowsNotificationData>& data) {
	QHash<quint32, WindowsNotification*> previous;
	for (auto* item: this->mNotifications.valueList()) previous.insert(item->id(), item);
	QList<WindowsNotification*> next;
	for (const auto& entry: data) {
		auto* item = previous.take(entry.id);
		if (item) item->update(entry);
		else item = new WindowsNotification(entry, this);
		next.append(item);
	}
	this->mNotifications.diffUpdate(next);
	for (auto* item: previous) item->deleteLater();
	if (!error.isEmpty() && error != this->bErrorString.value()) qCWarning(logHistory) << error;
	this->bAccess = access;
	this->bErrorString = error;
	this->bAvailable = access == Access::Allowed && error.isEmpty();
	this->bBusy = false;
}
