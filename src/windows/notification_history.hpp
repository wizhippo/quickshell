#pragma once

#include <memory>
#include <qdatetime.h>
#include <qobject.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qtimer.h>

#include "../core/doc.hpp"
#include "../core/model.hpp"

struct WindowsNotificationData {
	quint32 id = 0;
	QString appName;
	QString summary;
	QString body;
	QDateTime created;
};

///! A read-only toast in Windows' current notification history.
/// This does not expose actions, dismissal, or notification sending.
class WindowsNotification: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_UNCREATABLE("Notifications are acquired from NotificationHistory");
	// clang-format off
	Q_PROPERTY(quint32 id READ id CONSTANT);
	Q_PROPERTY(QString appName READ default NOTIFY appNameChanged BINDABLE bindableAppName);
	Q_PROPERTY(QString summary READ default NOTIFY summaryChanged BINDABLE bindableSummary);
	Q_PROPERTY(QString body READ default NOTIFY bodyChanged BINDABLE bindableBody);
	Q_PROPERTY(QDateTime created READ default NOTIFY createdChanged BINDABLE bindableCreated);
	// clang-format on
public:
	explicit WindowsNotification(const WindowsNotificationData& data, QObject* parent);
	[[nodiscard]] quint32 id() const { return this->mId; }
	QBindable<QString> bindableAppName() { return &this->bAppName; }
	QBindable<QString> bindableSummary() { return &this->bSummary; }
	QBindable<QString> bindableBody() { return &this->bBody; }
	QBindable<QDateTime> bindableCreated() { return &this->bCreated; }
	void update(const WindowsNotificationData& data);
signals:
	void appNameChanged();
	void summaryChanged();
	void bodyChanged();
	void createdChanged();
private:
	quint32 mId;
	// clang-format off
	Q_OBJECT_BINDABLE_PROPERTY(WindowsNotification, QString, bAppName, &WindowsNotification::appNameChanged);
	Q_OBJECT_BINDABLE_PROPERTY(WindowsNotification, QString, bSummary, &WindowsNotification::summaryChanged);
	Q_OBJECT_BINDABLE_PROPERTY(WindowsNotification, QString, bBody, &WindowsNotification::bodyChanged);
	Q_OBJECT_BINDABLE_PROPERTY(WindowsNotification, QDateTime, bCreated, &WindowsNotification::createdChanged);
	// clang-format on
};

///! Read-only Windows notification history, refreshed every ten seconds.
/// Only toasts still retained by Windows are visible. Access can be revoked
/// in Windows Settings; unavailable history is cleared rather than cached.
class NotificationHistory: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;
	// clang-format off
	QSDOC_TYPE_OVERRIDE(ObjectModel<WindowsNotification>*);
	Q_PROPERTY(UntypedObjectModel* notifications READ notifications CONSTANT);
	Q_PROPERTY(bool available READ default NOTIFY availableChanged BINDABLE bindableAvailable);
	Q_PROPERTY(bool busy READ default NOTIFY busyChanged BINDABLE bindableBusy);
	Q_PROPERTY(Access access READ default NOTIFY accessChanged BINDABLE bindableAccess);
	Q_PROPERTY(QString errorString READ default NOTIFY errorStringChanged BINDABLE bindableErrorString);
	// clang-format on
public:
	enum Access { Unspecified, Allowed, Denied, Unavailable };
	Q_ENUM(Access);
	explicit NotificationHistory(QObject* parent = nullptr);
	~NotificationHistory() override;
	ObjectModel<WindowsNotification>* notifications() { return &this->mNotifications; }
	QBindable<bool> bindableAvailable() { return &this->bAvailable; }
	QBindable<bool> bindableBusy() { return &this->bBusy; }
	QBindable<Access> bindableAccess() { return &this->bAccess; }
	QBindable<QString> bindableErrorString() { return &this->bErrorString; }
	/// Read history without showing a permission prompt.
	Q_INVOKABLE void refresh();
	/// Ask Windows for permission on the UI thread. Only call after user input.
	Q_INVOKABLE void requestAccess();
signals:
	void availableChanged();
	void busyChanged();
	void accessChanged();
	void errorStringChanged();
private:
	struct Native;
	std::unique_ptr<Native> native;
	void apply(Access access, const QString& error, const QList<WindowsNotificationData>& data);
	ObjectModel<WindowsNotification> mNotifications {this};
	QTimer timer;
	// clang-format off
	Q_OBJECT_BINDABLE_PROPERTY(NotificationHistory, bool, bAvailable, &NotificationHistory::availableChanged);
	Q_OBJECT_BINDABLE_PROPERTY(NotificationHistory, bool, bBusy, &NotificationHistory::busyChanged);
	Q_OBJECT_BINDABLE_PROPERTY(NotificationHistory, Access, bAccess, &NotificationHistory::accessChanged);
	Q_OBJECT_BINDABLE_PROPERTY(NotificationHistory, QString, bErrorString, &NotificationHistory::errorStringChanged);
	// clang-format on
};
