#pragma once

#include <qcontainerfwd.h>
#include <qfunctionpointer.h>
#include <qlist.h>

class EngineGeneration;

class QsEnginePlugin {
public:
	QsEnginePlugin() = default;
	virtual ~QsEnginePlugin() = default;
	QsEnginePlugin(QsEnginePlugin&&) = delete;
	QsEnginePlugin(const QsEnginePlugin&) = delete;
	void operator=(QsEnginePlugin&&) = delete;
	void operator=(const QsEnginePlugin&) = delete;

	virtual QString name() { return QString(); }
	virtual QList<QString> dependencies() { return {}; }
	virtual bool applies() { return true; }
	virtual void preinit() {}
	virtual void init() {}
	virtual void registerTypes() {}
	virtual void constructGeneration(EngineGeneration& /*unused*/) {} // NOLINT
	virtual void onReload() {}

	static void registerPlugin(QsEnginePlugin& plugin);
	static void preinitPluginsOnly();
	static void initPlugins();
	static void runConstructGeneration(EngineGeneration& generation);
	static void runOnReload();
};

// NOLINTBEGIN
// A namespace scope object with a constructor runs during static
// initialisation on every compiler. [[gnu::constructor]] would be tidier, but
// MSVC ignores it, which would leave no plugin registered at all.
// NOLINTEND
#define QS_REGISTER_PLUGIN(clazz)                                                                  \
	namespace {                                                                                      \
	struct QsPluginRegistrar_##__LINE__ {                                                            \
		QsPluginRegistrar_##__LINE__() {                                                             \
			static clazz plugin;                                                                     \
			QsEnginePlugin::registerPlugin(plugin);                                                  \
		}                                                                                            \
	} qsPluginRegistrar_##__LINE__;                                                                 \
	}
