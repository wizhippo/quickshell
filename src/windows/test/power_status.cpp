#include <QtTest>
#include "../power_reading.hpp"

class PowerStatusTest: public QObject {
	Q_OBJECT;
private slots:
	void chargingLaptop() {
		SYSTEM_POWER_STATUS native{1, 8, 72, 0, MAXDWORD, MAXDWORD};
		const auto reading = qs::windows::decodePowerStatus(native);
		QVERIFY(reading.available);
		QCOMPARE(reading.acStatus, 1);
		QCOMPARE(reading.batteryPresence, 1);
		QCOMPARE(reading.batteryPercent, 72);
		QVERIFY(reading.charging);
		QCOMPARE(reading.remainingSeconds, -1);
	}
	void desktopWithoutBattery() {
		// Contradictory/stale percentage and charging bits must not create a
		// fictitious battery when Windows explicitly reports no battery.
		SYSTEM_POWER_STATUS native{1, 128 | 8, 100, 0, 7200, 14400};
		const auto reading = qs::windows::decodePowerStatus(native);
		QCOMPARE(reading.batteryPresence, 0);
		QCOMPARE(reading.batteryPercent, -1);
		QVERIFY(!reading.charging);
		QCOMPARE(reading.remainingSeconds, -1);
	}
	void unknownFlagsAreNotBitMasks() {
		SYSTEM_POWER_STATUS native{255, 255, 255, 0, MAXDWORD, MAXDWORD};
		const auto reading = qs::windows::decodePowerStatus(native);
		QVERIFY(reading.available); // The API succeeded, but these fields are unknown.
		QCOMPARE(reading.acStatus, -1);
		QCOMPARE(reading.batteryPresence, -1);
		QCOMPARE(reading.batteryPercent, -1);
		QVERIFY(!reading.charging);
		QCOMPARE(reading.remainingSeconds, -1);
	}
	void emptyDischargingBatteryAndSaver() {
		SYSTEM_POWER_STATUS native{0, 4, 0, 1, 0, MAXDWORD};
		const auto reading = qs::windows::decodePowerStatus(native);
		QCOMPARE(reading.batteryPresence, 1);
		QCOMPARE(reading.batteryPercent, 0); // Zero is a valid charge reading.
		QCOMPARE(reading.remainingSeconds, 0); // Zero is a valid lifetime estimate.
		QVERIFY(!reading.charging);
		QVERIFY(reading.batterySaverEnabled);
	}
	void unknownAndInvalidPercentages_data() {
		QTest::addColumn<int>("percent");
		QTest::newRow("unknown") << 255;
		QTest::newRow("invalid") << 101;
	}
	void unknownAndInvalidPercentages() {
		QFETCH(int, percent);
		SYSTEM_POWER_STATUS native{0, 0, static_cast<BYTE>(percent), 0, 3600, MAXDWORD};
		const auto reading = qs::windows::decodePowerStatus(native);
		QCOMPARE(reading.batteryPresence, 1); // BatteryFlag 0 still means present.
		QCOMPARE(reading.batteryPercent, -1);
		QCOMPARE(reading.remainingSeconds, 3600);
	}
	void fullBatteryOnAcIsNotCharging() {
		SYSTEM_POWER_STATUS native{1, 1, 100, 0, 3600, MAXDWORD};
		const auto reading = qs::windows::decodePowerStatus(native);
		QCOMPARE(reading.batteryPercent, 100);
		QVERIFY(!reading.charging); // AC online does not imply charging.
		QCOMPARE(reading.remainingSeconds, -1); // Never show discharge estimates on AC.
	}
	void failedReadHasNoInventedValues() {
		const qs::windows::PowerReading reading;
		QVERIFY(!reading.available);
		QCOMPARE(reading.acStatus, -1);
		QCOMPARE(reading.batteryPresence, -1);
		QCOMPARE(reading.batteryPercent, -1);
		QVERIFY(!reading.charging);
		QCOMPARE(reading.remainingSeconds, -1);
	}
};

QTEST_GUILESS_MAIN(PowerStatusTest)
#include "power_status.moc"
