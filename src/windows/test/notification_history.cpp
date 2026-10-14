#include "../notification_history.hpp"
#include <qguiapplication.h>
#include <qtest.h>

class NotificationHistoryTest: public QObject {
	Q_OBJECT;
private slots:
	void nativeRead() {
		NotificationHistory history;
		QTRY_VERIFY_WITH_TIMEOUT(!history.bindableBusy().value(), 10000);
		if (history.bindableAvailable().value()) {
			QCOMPARE(history.bindableAccess().value(), NotificationHistory::Allowed);
			QVERIFY(history.bindableErrorString().value().isEmpty());
			QDateTime previous;
			QSet<quint32> ids;
			for (auto* item: history.notifications()->valueList()) {
				QVERIFY(!ids.contains(item->id()));
				ids.insert(item->id());
				auto created = item->bindableCreated().value();
				QVERIFY(created.isValid());
				QVERIFY(!previous.isValid() || created <= previous);
				previous = created;
			}
		} else {
			QVERIFY(!history.bindableErrorString().value().isEmpty());
			QCOMPARE(history.notifications()->rowCount({}), 0);
			qInfo() << history.bindableErrorString().value();
		}
		history.refresh();
		QVERIFY(history.bindableBusy().value());
		QTRY_VERIFY_WITH_TIMEOUT(!history.bindableBusy().value(), 10000);
	}
	void destructionDuringRead() {
		// A reload must disconnect the worker result before it reaches QML.
		auto* history = new NotificationHistory;
		QVERIFY(history->bindableBusy().value());
		delete history;
		QTest::qWait(500);
		NotificationHistory replacement;
		QTRY_VERIFY_WITH_TIMEOUT(!replacement.bindableBusy().value(), 10000);
	}
};
QTEST_MAIN(NotificationHistoryTest);
#include "notification_history.moc"
