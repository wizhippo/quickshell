#include "../system.hpp"
#include <qcoreapplication.h>
#include <qdir.h>
#include <qsignalspy.h>
#include <qtest.h>
#include <quuid.h>

class SystemTest: public QObject {
	Q_OBJECT;
public:
	bool openRealSettings = false;
private slots:
	void invalidPage() {
		System service;
		QSignalSpy finished(&service, &System::openFinished);
		for (const auto& page: {QString(), QString("../network-wifi"), QString("network-wifi?extra"), QString("https://example.com")}) {
			service.openSettings(page);
			QCOMPARE(finished.count(), 1);
			const auto result = finished.takeFirst();
			QCOMPARE(result.at(0).toBool(), false);
			QCOMPARE(result.at(1).toString(), page);
		}
	}
	void missingTarget() {
		System service;
		QSignalSpy finished(&service, &System::openFinished);
		const auto target = QDir::tempPath() + "/quickshell-missing-" + QUuid::createUuid().toString(QUuid::Id128) + ".txt";
		service.open(target);
		QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
		QCOMPARE(finished.first().at(0).toBool(), false);
		QCOMPARE(finished.first().at(1).toString(), target);
	}
	void destructionDuringRequest() {
		auto* service = new System;
		service->open(QDir::tempPath() + "/quickshell-missing-" + QUuid::createUuid().toString(QUuid::Id128));
		delete service;
		QTest::qWait(500);
	}
	void settingsSmoke() {
		if (!this->openRealSettings) QSKIP("Use --open-settings to open real Settings pages on the desktop.");
		System service;
		QSignalSpy finished(&service, &System::openFinished);
		for (const auto& page: {QString("network-wifi"), QString("powersleep")}) {
			service.openSettings(page);
			QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 20000);
			const auto result = finished.takeFirst();
			QVERIFY2(result.at(0).toBool(), qPrintable("Activation failed for ms-settings:" + page));
			QCOMPARE(result.at(1).toString(), "ms-settings:" + page);
		}
	}
};

int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);
	auto arguments = app.arguments();
	SystemTest test;
	test.openRealSettings = arguments.removeAll("--open-settings") > 0;
	return QTest::qExec(&test, arguments);
}
#include "system.moc"
