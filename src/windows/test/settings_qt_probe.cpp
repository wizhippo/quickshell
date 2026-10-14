#include <QDesktopServices>
#include <QGuiApplication>
#include <QProcess>
#include <QTimer>
#include <QUrl>
#include <cstdio>

#include "settings_presentation.hpp"

int main(int argc, char** argv) {
	setvbuf(stdout, nullptr, _IONBF, 0);
	QGuiApplication application(argc, argv);
	application.setQuitOnLastWindowClosed(false);
	const auto mode = application.arguments().value(1, QStringLiteral("url"));
	if (mode != "url" && mode != "separate" && mode != "thispc") return 2;
	qint64 helperPid = 0;
	if (mode != "url") {
		const auto args = mode == "separate"
		    ? QStringList {QStringLiteral("/separate"), QStringLiteral("C:\\")}
		    : QStringList {QStringLiteral("shell:::{20D04FE0-3AEA-1069-A2D8-08002B30309D}")};
		const bool started = QProcess::startDetached(QStringLiteral("explorer.exe"), args, {}, &helperPid);
		std::printf("QT Explorer mode=%s started=%d pid=%lld\n", qPrintable(mode), started, helperPid);
		if (!started) return 3;
	}
	QTimer::singleShot(mode == "url" ? 0 : 3000, &application, [&] {
		DWORD shellPid = 0;
		GetWindowThreadProcessId(GetShellWindow(), &shellPid);
		std::printf("QT before openUrl: shellPid=%lu helperPid=%lld\n", shellPid, helperPid);
		// The user's exact URI/parsing choice, using Qt's actual Windows backend.
		const bool opened = QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:"), QUrl::TolerantMode));
		std::printf("QT openUrl returned=%d\n", opened);
		QTimer::singleShot(3000, &application, [&, opened] {
			const auto windows = qs::windows::test::SettingsSnapshot::read();
			DWORD frameCloak = 99, coreCloak = 99, shellPid = 0;
			if (windows.frame) DwmGetWindowAttribute(windows.frame, DWMWA_CLOAKED, &frameCloak, sizeof(frameCloak));
			if (windows.core) DwmGetWindowAttribute(windows.core, DWMWA_CLOAKED, &coreCloak, sizeof(coreCloak));
			GetWindowThreadProcessId(GetShellWindow(), &shellPid);
			const bool visible = windows.frames == 1 && windows.cores == 1 && GetParent(windows.core) == windows.frame
			    && IsWindowVisible(windows.frame) && IsWindowVisible(windows.core) && !frameCloak && !coreCloak;
			std::printf("QT result: opened=%d visible=%d frames=%u cores=%u cloak=%lu/%lu shellPid=%lu\n",
			            opened, visible, windows.frames, windows.cores, frameCloak, coreCloak, shellPid);
			application.exit(opened && visible ? 0 : 1);
		});
	});
	return application.exec();
}
