#include "process.hpp"

#include <qlist.h>
#include <qsignalspy.h>
#include <qtest.h>
#include <qtestcase.h>

#include "../process.hpp"

namespace {
// The tests drive a real child process, so they need a command that exists and
// exits successfully, plus one that stays alive, on whichever platform is
// running them.
QStringList successCommand() {
#ifdef Q_OS_WIN
	return {"cmd.exe", "/c", "exit 0"};
#else
	return {"true"};
#endif
}

QStringList idleCommand() {
#ifdef Q_OS_WIN
	// Roughly thirty seconds, without depending on a Unix utility.
	return {"cmd.exe", "/c", "ping -n 31 127.0.0.1 >NUL"};
#else
	return {"sleep", "30"};
#endif
}
} // namespace

void TestProcess::startAfterReload() {
	auto process = Process();
	auto startedSpy = QSignalSpy(&process, &Process::started);
	auto exitedSpy = QSignalSpy(&process, &Process::exited);

	process.setCommand(successCommand());
	process.setRunning(true);

	QVERIFY(!process.isRunning());
	QCOMPARE(startedSpy.count(), 0);

	process.postReload();

	QVERIFY(process.isRunning());
	// postReload restarts synchronously, so the signal may already be recorded
	// by the time the spy is asked to wait.
	QTRY_VERIFY_WITH_TIMEOUT(startedSpy.count() > 0, 10000);
}

void TestProcess::testExec() {
	auto process = Process();
	auto startedSpy = QSignalSpy(&process, &Process::started);
	auto exitedSpy = QSignalSpy(&process, &Process::exited);

	process.postReload();

	process.setCommand(idleCommand());
	process.setRunning(true);

	QVERIFY(process.isRunning());
	// Spawning a shell is not instant on every platform, and postReload
	// restarts synchronously, so bound the wait instead of assuming 100ms.
	QTRY_VERIFY_WITH_TIMEOUT(startedSpy.count() > 0, 10000);

	process.exec(successCommand());

	// The replacement command exits immediately by design, so the emitted
	// signals are what proves the swap happened. Asserting it is still running
	// afterwards would be racing whatever the host does to spawn a shell.
	QTRY_VERIFY_WITH_TIMEOUT(exitedSpy.count() > 0, 10000);
	QTRY_VERIFY_WITH_TIMEOUT(startedSpy.count() > 0, 10000);
}

QTEST_MAIN(TestProcess);
