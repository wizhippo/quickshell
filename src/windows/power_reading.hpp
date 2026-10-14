#pragma once

#include <QtTypes>
#include <windows.h>

namespace qs::windows {

struct PowerReading {
	bool available = false;
	int acStatus = -1; // -1 unknown, 0 offline, 1 online.
	int batteryPresence = -1; // -1 unknown, 0 absent, 1 present.
	int batteryPercent = -1;
	bool charging = false;
	bool batterySaverEnabled = false;
	qint64 remainingSeconds = -1;
	bool operator==(const PowerReading&) const = default;
};

inline PowerReading decodePowerStatus(const SYSTEM_POWER_STATUS& native) {
	PowerReading reading;
	reading.available = true;
	if (native.ACLineStatus <= 1) reading.acStatus = native.ACLineStatus;
	// 255 means unknown, not a combination of all the battery flag bits.
	if (native.BatteryFlag != 255) {
		reading.batteryPresence = (native.BatteryFlag & 128) ? 0 : 1;
	}
	if (reading.batteryPresence == 1) {
		if (native.BatteryLifePercent <= 100) reading.batteryPercent = native.BatteryLifePercent;
		reading.charging = (native.BatteryFlag & 8) != 0;
		if (reading.acStatus == 0 && native.BatteryLifeTime != MAXDWORD) {
			reading.remainingSeconds = native.BatteryLifeTime;
		}
	}
	reading.batterySaverEnabled = native.SystemStatusFlag == 1;
	return reading;
}

} // namespace qs::windows
