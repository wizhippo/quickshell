#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QWindow>
#include <qqmlintegration.h>
#include <vector>
#include "power_reading.hpp"

///! System power status as reported by `GetSystemPowerStatus`.
/// Each QML engine owns one instance and its native notification
/// registrations, so reloading leaves no stale filters behind. Check
/// @@available before reading the numeric and boolean properties, which fall
/// back to unknown values when the last query failed.
class PowerStatus: public QObject, public QAbstractNativeEventFilter {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;
	/// True while the last power query succeeded. Also true on desktops
	/// without a battery.
	Q_PROPERTY(bool available READ available NOTIFY statusChanged);
	/// Whether the system is running on AC power.
	Q_PROPERTY(AcStatus acStatus READ acStatus NOTIFY statusChanged);
	/// Whether a battery is installed.
	Q_PROPERTY(BatteryPresence batteryPresence READ batteryPresence NOTIFY statusChanged);
	/// Charge level 0-100, or -1 when unknown or not applicable.
	Q_PROPERTY(int batteryPercent READ batteryPercent NOTIFY statusChanged);
	/// Whether Windows reports an installed battery is charging. AC power alone
	/// does not imply charging.
	Q_PROPERTY(bool charging READ charging NOTIFY statusChanged);
	/// Whether Windows reports battery saver is on.
	Q_PROPERTY(bool batterySaverEnabled READ batterySaverEnabled NOTIFY statusChanged);
	/// Windows' remaining time estimate on battery, or -1 when unknown.
	Q_PROPERTY(qint64 remainingSeconds READ remainingSeconds NOTIFY statusChanged);
	/// Query failure details, empty on success.
	Q_PROPERTY(QString errorString READ errorString NOTIFY statusChanged);

public:
	enum AcStatus { AcUnknown = -1, AcOffline = 0, AcOnline = 1 };
	Q_ENUM(AcStatus);
	enum BatteryPresence { BatteryUnknown = -1, NoBattery = 0, HasBattery = 1 };
	Q_ENUM(BatteryPresence);

	explicit PowerStatus(QObject* parent = nullptr);
	~PowerStatus() override;
	bool available() const { return reading.available; }
	AcStatus acStatus() const { return static_cast<AcStatus>(reading.acStatus); }
	BatteryPresence batteryPresence() const { return static_cast<BatteryPresence>(reading.batteryPresence); }
	int batteryPercent() const { return reading.batteryPercent; }
	bool charging() const { return reading.charging; }
	bool batterySaverEnabled() const { return reading.batterySaverEnabled; }
	qint64 remainingSeconds() const { return reading.remainingSeconds; }
	QString errorString() const { return lastError; }
	Q_INVOKABLE void refresh();
	bool nativeEventFilter(const QByteArray& type, void* message, qintptr* result) override;

signals:
	void statusChanged();

private:
	void scheduleRefresh();
	qs::windows::PowerReading reading;
	QString lastError;
	QTimer timer;
	QWindow notificationWindow;
	HWND notificationHandle = nullptr;
	std::vector<HPOWERNOTIFY> registrations;
	bool refreshPending = false;
};
