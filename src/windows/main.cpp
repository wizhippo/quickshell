#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcessEnvironment>
#include <QQuickWindow>
#include <QTimer>
#include <QSessionManager>
#include <QStandardPaths>
#include <memory>

#include "explorer_session.hpp"
#include "immersive_host.hpp"

#include "../core/common.hpp"
#include "../core/debuginfo.hpp"
#include "../core/instanceinfo.hpp"
#include "../core/logging.hpp"
#include "../core/paths.hpp"
#include "../core/plugin.hpp"
#include "../core/qmlglobal.hpp"
#include "../core/rootwrapper.hpp"

QS_LOGGING_CATEGORY(logImmersiveHost, "quickshell.windows.immersive", QtInfoMsg);

static int runShell(int argc, char** argv, bool& offerRestore) {
	QGuiApplication app(argc, argv);
	QCoreApplication::setApplicationName("quickshell");
	QCoreApplication::setApplicationVersion(qs::debuginfo::qsVersion());
	QGuiApplication::setQuitOnLastWindowClosed(false);

	QCommandLineParser parser;
	parser.setApplicationDescription("Quickshell Windows experimental native AppBar port");
	parser.addHelpOption();
	parser.addVersionOption();
	parser.addOption({{"p", "path"}, "QML configuration file or directory containing shell.qml.", "path"});
	parser.addOption({"quit-after", "Exit after this many milliseconds (runtime verification).", "milliseconds"});
	parser.addOption({"manage-explorer", "Ask to replace Explorer on startup and restore it after exit."});
	parser.addOption({"no-immersive-host", "Disable the experimental Settings host for session diagnostics."});
	parser.addPositionalArgument("config", "QML configuration file or directory.", "[config]");
	parser.process(app);

	auto path = parser.value("path");
	if (path.isEmpty() && !parser.positionalArguments().isEmpty()) path = parser.positionalArguments().first();
	if (path.isEmpty()) path = "shell.qml";
	if (QFileInfo(path).isDir()) path = QDir(path).filePath("shell.qml");
	path = QFileInfo(path).absoluteFilePath();
	if (!QFileInfo(path).isFile()) {
		qCritical() << "Configuration file not found:" << path;
		return 1;
	}
	int quitDelay = 0;
	if (parser.isSet("quit-after")) {
		bool ok = false;
		quitDelay = parser.value("quit-after").toInt(&ok);
		if (!ok || quitDelay < 1) {
			qCritical() << "--quit-after must be a positive integer.";
			return 1;
		}
	}
	if (parser.isSet("manage-explorer")) {
		bool stoppedExplorer = false;
		if (!qs::windows::confirmExplorerReplacement(stoppedExplorer)) {
			offerRestore = stoppedExplorer;
			return 0;
		}
		offerRestore = true;
		QObject::connect(&app, &QGuiApplication::commitDataRequest, &app,
		                 [&offerRestore](QSessionManager&) { offerRestore = false; });
	}

	qs::Common::INITIAL_ENVIRONMENT = QProcessEnvironment::systemEnvironment();
	const auto shellId = QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256).toHex());
	InstanceInfo::CURRENT = {
		.instanceId = QString::number(app.applicationPid()) + "-" + QString::number(qs::Common::LAUNCH_TIME.toMSecsSinceEpoch()),
		.configPath = path,
		.shellId = shellId,
		.appId = "org.quickshell.windows",
		.launchTime = qs::Common::LAUNCH_TIME,
		.pid = static_cast<qint32>(app.applicationPid()),
		.display = "windows",
	};
	QsPaths::init(shellId, shellId, {}, {}, {});
	LogManager::init(false, false, true, QtInfoMsg, {});
	QuickshellTracked::init();
	QsEnginePlugin::initPlugins();
	QQuickWindow::setDefaultAlphaBuffer(true);

	const auto symbolCache = qEnvironmentVariable("QS_WINDOWS_SYMBOL_CACHE",
	    QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath("windows-symbols"));
	std::unique_ptr<qs::windows::ImmersiveHost> immersiveHost;
	if (!parser.isSet("no-immersive-host")) {
		immersiveHost = std::make_unique<qs::windows::ImmersiveHost>(
		    std::filesystem::path(symbolCache.toStdWString()),
		    [](bool warning, const std::string& message) {
			    if (warning) qCWarning(logImmersiveHost).noquote() << QString::fromStdString(message).trimmed();
			    else qCInfo(logImmersiveHost).noquote() << QString::fromStdString(message).trimmed();
		    });
	} else {
		qCInfo(logImmersiveHost) << "Experimental Settings host disabled for session diagnostics.";
	}
	// Keep the GUI event loop responsive while symbols/controller initialize.
	// QML services start after the host so they cannot race its shell handoff.
	std::unique_ptr<RootWrapper> root;
	QTimer startup;
	const auto loadRoot = [&] {
		if (immersiveHost && immersiveHost->state() == qs::windows::ImmersiveHost::State::Starting) return;
		startup.stop();
		root = std::make_unique<RootWrapper>(path, shellId);
	};
	QObject::connect(&startup, &QTimer::timeout, &app, loadRoot);
	startup.start(50);
	loadRoot();
	if (quitDelay > 0) QTimer::singleShot(quitDelay, &app, &QCoreApplication::quit);
	return app.exec();
}

int main(int argc, char** argv) {
	bool offerRestore = false;
	const auto result = runShell(argc, argv, offerRestore);
	// QML, the QGuiApplication and our native tray have all been destroyed first.
	if (offerRestore) qs::windows::offerExplorerRestart();
	return result;
}
