// Windows console logger. Category filtering and formatting are shared with upstream;
// the Unix mmap/fcntl encoded file logger is intentionally excluded.
#include "../core/logging.hpp"
#include <cstdio>
#include <QCoreApplication>
#include <QMutexLocker>
#include <QTextStream>
#include <QThread>
#include "../core/logging_qtprivate.cpp"
QS_LOGGING_CATEGORY(logBare, "quickshell.bare");
namespace qs::log {
using namespace qt_logging_registry;
QS_LOGGING_CATEGORY(logLogging, "quickshell.logging", QtWarningMsg);
bool LogMessage::operator==(const LogMessage& other) const {
	// note: not including time
	return this->type == other.type && this->category == other.category && this->body == other.body;
}

size_t qHash(const LogMessage& message) {
	return qHash(message.type) ^ qHash(message.category) ^ qHash(message.body);
}

void LogMessage::formatMessage(
    QTextStream& stream,
    const LogMessage& msg,
    bool color,
    bool timestamp,
    const QString& prefix
) {
	if (!prefix.isEmpty()) {
		if (color) stream << "\033[90m";
		stream << '[' << prefix << ']';
		if (timestamp) stream << ' ';
		if (color) stream << "\033[0m";
	}

	if (timestamp) {
		if (color) stream << "\033[90m";
		stream << msg.time.toString("yyyy-MM-dd hh:mm:ss.zzz");
	}

	if (msg.category == "quickshell.bare") {
		if (!prefix.isEmpty()) stream << ' ';
		stream << msg.body;
	} else {
		if (color) {
			switch (msg.type) {
			case QtDebugMsg: stream << "\033[34m DEBUG"; break;
			case QtInfoMsg: stream << "\033[32m  INFO"; break;
			case QtWarningMsg: stream << "\033[33m  WARN"; break;
			case QtCriticalMsg: stream << "\033[31m ERROR"; break;
			case QtFatalMsg: stream << "\033[31m FATAL"; break;
			}
		} else {
			switch (msg.type) {
			case QtDebugMsg: stream << " DEBUG"; break;
			case QtInfoMsg: stream << "  INFO"; break;
			case QtWarningMsg: stream << "  WARN"; break;
			case QtCriticalMsg: stream << " ERROR"; break;
			case QtFatalMsg: stream << " FATAL"; break;
			}
		}

		const auto isDefault = msg.category == "default";

		if (color && !isDefault && msg.type != QtFatalMsg) stream << "\033[97m";

		if (!isDefault) {
			stream << ' ' << msg.category;
		}

		if (color && msg.type != QtFatalMsg) stream << "\033[0m";

		stream << ": " << msg.body;

		if (color && msg.type == QtFatalMsg) stream << "\033[0m";
	}
}

bool CategoryFilter::shouldDisplay(QtMsgType type) const {
	switch (type) {
	case QtDebugMsg: return this->debug;
	case QtInfoMsg: return this->info;
	case QtWarningMsg: return this->warn;
	case QtCriticalMsg: return this->critical;
	default: return true;
	}
}

void CategoryFilter::apply(QLoggingCategory* category) const {
	category->setEnabled(QtDebugMsg, this->debug);
	category->setEnabled(QtInfoMsg, this->info);
	category->setEnabled(QtWarningMsg, this->warn);
	category->setEnabled(QtCriticalMsg, this->critical);
}

void CategoryFilter::applyRule(
    QLatin1StringView category,
    const qt_logging_registry::QLoggingRule& rule
) {
	auto filterpass = rule.pass(category, QtDebugMsg);
	if (filterpass != 0) this->debug = filterpass > 0;

	filterpass = rule.pass(category, QtInfoMsg);
	if (filterpass != 0) this->info = filterpass > 0;

	filterpass = rule.pass(category, QtWarningMsg);
	if (filterpass != 0) this->warn = filterpass > 0;

	filterpass = rule.pass(category, QtCriticalMsg);
	if (filterpass != 0) this->critical = filterpass > 0;
}

LogManager::LogManager(): stdoutStream(stdout) {}

void LogManager::messageHandler(
    QtMsgType type,
    const QMessageLogContext& context,
    const QString& msg
) {
	auto message = LogMessage(type, QLatin1StringView(context.category), msg.toUtf8());

	auto* self = LogManager::instance();

	auto display = true;

	const auto* key = static_cast<const void*>(context.category);

	if (self->sparseFilters.contains(key)) {
		display = self->sparseFilters.value(key).shouldDisplay(type);
	}

	if (display) {
		auto locker = QMutexLocker(&self->stdoutMutex);
		LogMessage::formatMessage(
		    self->stdoutStream,
		    message,
		    self->colorLogs,
		    self->timestampLogs,
		    self->prefix
		);

		self->stdoutStream << Qt::endl;
	}

	emit self->logMessage(message, display);
}

void LogManager::filterCategory(QLoggingCategory* category) {
	auto* instance = LogManager::instance();

	auto categoryName = QLatin1StringView(category->categoryName());
	auto isQs = categoryName.startsWith(QLatin1StringView("quickshell."));

	// The Windows launcher initializes after QApplication. Preserve Qt's
	// existing category state when there is no previous custom filter.
	CategoryFilter filter(category);

	// We don't respect log filters for qs logs because some distros like to ship
	// default configs that hide everything. QT_LOGGING_RULES is considered via the filter list.
	if (isQs) {
		// QtDebugMsg == 0, so default
		auto defaultLevel = instance->defaultLevels.value(categoryName);

		filter = CategoryFilter();
		// clang-format off
		filter.debug = instance->mDefaultLevel == QtDebugMsg || defaultLevel == QtDebugMsg;
		filter.info = filter.debug || instance->mDefaultLevel == QtInfoMsg || defaultLevel == QtInfoMsg;
		filter.warn = filter.info || instance->mDefaultLevel == QtWarningMsg || defaultLevel == QtWarningMsg;
		filter.critical = filter.warn || instance->mDefaultLevel == QtCriticalMsg || defaultLevel == QtCriticalMsg;
		// clang-format on
	} else {
		if (instance->lastCategoryFilter) {
			instance->lastCategoryFilter(category);
			filter = CategoryFilter(category);
		}

		// Hides virtual/override property warnings.
		// Getting rid of this is blocked by https://qt-project.atlassian.net/browse/QTBUG-145977
		// for internal Quickshell types, and may still be desired for shells that want to maintain
		// compatibility with Qt versions prior to 6.11.
		if (categoryName == QLatin1StringView("qt.qml.propertyCache.append")
		    && !qEnvironmentVariableIsSet("QS_NO_FILTER_QT_LOGS"))
		{
			filter.warn = false;
		}
	}

	for (const auto& rule: *instance->rules) {
		filter.applyRule(categoryName, rule);
	}

	if (isQs && !instance->sparse) {
		// We assume the category name pointer will always be the same and be comparable in the message handler.
		instance->sparseFilters.insert(static_cast<const void*>(category->categoryName()), filter);

		// all enabled by default
		CategoryFilter().apply(category);
	} else {
		filter.apply(category);
	}

	instance->allFilters.insert(categoryName, filter);
}

LogManager* LogManager::instance() {
	static auto* instance = new LogManager(); // NOLINT
	return instance;
}

void LogManager::init(
    bool color,
    bool timestamp,
    bool sparseOnly,
    QtMsgType defaultLevel,
    const QString& rules,
    const QString& prefix
) {
	static bool alreadyInitialized = false;
	if (alreadyInitialized) return;
	alreadyInitialized = true;

	auto* instance = LogManager::instance();
	instance->colorLogs = color;
	instance->timestampLogs = timestamp;
	instance->sparse = sparseOnly;
	instance->prefix = prefix;
	instance->mDefaultLevel = defaultLevel;
	instance->mRulesString = rules;

	{
		QLoggingSettingsParser parser;
		// Load QT_LOGGING_RULES because we ignore the last category filter for QS messages
		// due to disk config files.
		parser.setContent(qEnvironmentVariable("QT_LOGGING_RULES"));
		instance->rules = new QList(parser.rules());
		parser.setContent(rules);
		instance->rules->append(parser.rules());
	}

	instance->lastCategoryFilter = QLoggingCategory::installFilter(&LogManager::filterCategory);

	if (instance->lastCategoryFilter == &LogManager::filterCategory) {
		qCFatal(logLogging) << "Quickshell's log filter has been installed twice. This is a bug.";
		instance->lastCategoryFilter = nullptr;
	}

	qInstallMessageHandler(&LogManager::messageHandler);


	qCDebug(logLogging) << "Logger initialized.";
}

void LogManager::initThreadLogging() { /* Windows milestone uses console logging only. */ }

void initLogCategoryLevel(const char* name, QtMsgType defaultLevel) {
	LogManager::instance()->defaultLevels.insert(QLatin1StringView(name), defaultLevel);
}

void LogManager::initFs() { /* Encoded file logging is not available in this milestone. */ }

QString LogManager::rulesString() const { return this->mRulesString; }
QtMsgType LogManager::defaultLevel() const { return this->mDefaultLevel; }
bool LogManager::isSparse() const { return this->sparse; }

CategoryFilter LogManager::getFilter(QLatin1StringView category) {
	return this->allFilters.value(category);
}


} // namespace qs::log
