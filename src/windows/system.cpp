#include "system.hpp"
#include "immersive_host.hpp"
#include <utility>

#include <qdir.h>
#include <qcoreapplication.h>
#include <qloggingcategory.h>
#include <qpointer.h>
#include <qregularexpression.h>
#include <qscopeguard.h>
#include <qthreadpool.h>
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include "../core/logcat.hpp"

namespace {
QS_LOGGING_CATEGORY(logSystem, "quickshell.windows.system", QtWarningMsg);

// Settings pages are a small fixed vocabulary. Matching it exactly means a
// page can never carry a scheme, a path or anything else into the URI.
const QRegularExpression& settingsPagePattern() {
	static const QRegularExpression pattern(QStringLiteral("^[a-z0-9]+(-[a-z0-9]+)*$"));
	return pattern;
}

// Runs on a worker thread. ShellExecuteEx blocks until the handler has handed
// off, which is exactly the stall this keeps off the GUI thread.
bool shellExecute(const QString& verb, const QString& target) {
	const auto apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE) {
		qCWarning(logSystem) << "Could not initialize shell activation COM apartment:"
		                    << QStringLiteral("0x%1").arg(static_cast<quint32>(apartment), 8, 16, QLatin1Char('0'));
		return false;
	}
	// COM belongs to a thread, not the process. Balance S_OK and S_FALSE;
	// RPC_E_CHANGED_MODE means an existing apartment that we do not own.
	auto uninitialize = qScopeGuard([apartment] { if (SUCCEEDED(apartment)) CoUninitialize(); });
	SHELLEXECUTEINFOW info {};
	info.cbSize = sizeof(info);
	info.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC | SEE_MASK_NOCLOSEPROCESS;
	info.lpVerb = verb.isEmpty() ? nullptr : reinterpret_cast<LPCWSTR>(verb.utf16());
	info.lpFile = reinterpret_cast<LPCWSTR>(target.utf16());
	info.nShow = SW_SHOWNORMAL;
	if (!ShellExecuteExW(&info)) {
		const auto error = GetLastError();
		qCWarning(logSystem) << "Could not open" << target
		                    << QStringLiteral("(0x%1)").arg(error, 8, 16, QLatin1Char('0'));
		return false;
	}
	// Release the handle promptly; the handler outliving this call is its own
	// business, and holding it would leak on every launch.
	if (info.hProcess != nullptr) CloseHandle(info.hProcess);
	return true;
}
} // namespace

System::System(QObject* parent): QObject(parent) {}

void System::lockSession() {
	// Not worth a scripting helper: this is one user32 call, and the shell is
	// already a native process.
	if (LockWorkStation()) return;
	qCWarning(logSystem) << "Could not lock the workstation (" << GetLastError() << ')';
}

void System::open(const QString& target) {
	if (target.isEmpty()) {
		qCWarning(logSystem) << "Refusing to open an empty target";
		emit this->openFinished(false, target);
		return;
	}
	this->runAsync(target, QString());
}

void System::openSettings(const QString& page) {
	if (page.isEmpty() || !settingsPagePattern().match(page).hasMatch()) {
		qCWarning(logSystem) << "Refusing an invalid settings page:" << page;
		emit this->openFinished(false, page);
		return;
	}
	this->open(QStringLiteral("ms-settings:") + page);
}

void System::openFolder(const QString& path) {
	// An empty path means the working directory, which is what a caller asking
	// to "open a folder" with no folder almost always wants.
	auto target = path.isEmpty() ? QDir::currentPath() : QDir::toNativeSeparators(path);
	this->runAsync(target, QStringLiteral("open"));
}

void System::runAsync(const QString& target, const QString& verb) {
	QPointer<System> guard(this);
	QThreadPool::globalInstance()->start([guard, target, verb] {
		const auto success = shellExecute(verb, target);
		if (success && target.startsWith(QStringLiteral("ms-settings:"), Qt::CaseInsensitive))
			qs::windows::ImmersiveHost::requestSettingsActivation();
		QMetaObject::invokeMethod(
		    qApp,
		    [guard, target, success] { if (guard) emit guard->openFinished(success, target); },
		    Qt::QueuedConnection
		);
	});
}
