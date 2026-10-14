#include <QGuiApplication>
#include <qqml.h>
#include "../core/plugin.hpp"
#include "panel_window.hpp"

namespace {
class WindowsPlugin: public QsEnginePlugin {
	QString name() override { return "windows"; }
	QList<QString> dependencies() override { return {"window"}; }
	bool applies() override { return QGuiApplication::platformName() == "windows"; }
	void registerTypes() override {
		// The Quickshell.Windows singletons are registered declaratively by
		// quickshell-windows' QML module. Only the platform overlay, which
		// shadows Quickshell's window types, is registered by hand.
		qmlRegisterType<WinPanelInterface>("Quickshell._WindowsOverlay", 1, 0, "PanelWindow");
		qmlRegisterModuleImport("Quickshell", QQmlModuleImportModuleAny,
		                        "Quickshell._WindowsOverlay", QQmlModuleImportLatest);
	}
};
QS_REGISTER_PLUGIN(WindowsPlugin);
}