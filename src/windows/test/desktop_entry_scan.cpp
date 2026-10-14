#include "../desktop_entry_scan.hpp"
#include "../../core/desktopentry.hpp"
#include <qfile.h>
#include <qguiapplication.h>
#include <qjsonarray.h>
#include <qjsondocument.h>
#include <qjsonobject.h>
#include <qtest.h>

class ApplicationScanTest: public QObject {
	Q_OBJECT;
private slots:
	void nativeEnumeration() {
		auto scan = qs::windows::scanApplications();
		QCOMPARE(scan.error, 0u);
		QVERIFY(!scan.entries.empty());
		std::wstring previous;
		for (const auto& entry: scan.entries) {
			QVERIFY(!entry.id.empty());
			QVERIFY(!entry.name.empty());
			QVERIFY(entry.id > previous); // Sorted and deduplicated.
			QVERIFY(!entry.id.starts_with(L"::{4234d49b-0245-4df3-b780-3893943456e1}"));
			previous = entry.id;
		}
	}
	void desktopEntriesIntegration() {
		DesktopEntries entries;
		QTRY_VERIFY_WITH_TIMEOUT(!DesktopEntries::loading(), 30000);
		QVERIFY2(DesktopEntries::errorString().isEmpty(), qPrintable(DesktopEntries::errorString()));
		auto applications = DesktopEntries::applications()->valueList();
		QVERIFY(!applications.isEmpty());
		auto* retained = applications.first();
		const auto id = retained->mId;
		QCOMPARE(DesktopEntries::byId(id), retained);
		QCOMPARE(retained->bindableCommand().value(), QList<QString>({"explorer.exe", "shell:AppsFolder\\" + id}));
		DesktopEntries::refresh();
		QVERIFY(DesktopEntries::loading());
		QTRY_VERIFY_WITH_TIMEOUT(!DesktopEntries::loading(), 30000);
		QVERIFY2(DesktopEntries::errorString().isEmpty(), qPrintable(DesktopEntries::errorString()));
		QCOMPARE(DesktopEntries::byId(id), retained);
	}
};

int main(int argc, char** argv) {
	QGuiApplication app(argc, argv);
	// Read-only comparison artifact for checking AppsFolder against Get-StartApps.
	const auto args = app.arguments();
	const auto dump = args.indexOf("--dump");
	if (dump >= 0 && dump + 1 < args.length()) {
		const auto scan = qs::windows::scanApplications();
		if (scan.error) return 1;
		QJsonArray result;
		for (const auto& entry: scan.entries)
			result.append(QJsonObject {{"id", QString::fromStdWString(entry.id)}, {"name", QString::fromStdWString(entry.name)}});
		QFile file(args.at(dump + 1));
		if (!file.open(QIODevice::WriteOnly)) return 2;
		return file.write(QJsonDocument(result).toJson()) >= 0 ? 0 : 3;
	}
	ApplicationScanTest test;
	return QTest::qExec(&test, argc, argv);
}
#include "desktop_entry_scan.moc"
