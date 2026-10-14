#pragma once

#include <qobject.h>
#include <qqmlintegration.h>
#include <qtmetamacros.h>
#include <qstring.h>

///! Windows session and shell integration.
/// Actions that belong to Windows itself, so a shell can reach them without
/// scripting a helper process.
///
/// Each is an @@EngineGeneration object: handlers may run on a worker thread
/// because activating a URI can stall behind a hung handler.
class System: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	explicit System(QObject* parent = nullptr);

	/// Lock the workstation, as Win+L does.
	///
	/// Reports failure through a warning rather than a signal, because there is
	/// nothing a shell can usefully do about a refusal.
	Q_INVOKABLE void lockSession();

	/// Open a URI or file path with whatever Windows has registered for it.
	///
	/// Runs off the GUI thread, because a handler that never returns would
	/// otherwise freeze the shell. Reports completion through @@openFinished().
	Q_INVOKABLE void open(const QString& target);

	/// Open a Windows settings page, such as `network-wifi`.
	///
	/// The page name is validated here rather than interpolated by QML, so a
	/// page can never become part of a different URI.
	Q_INVOKABLE void openSettings(const QString& page);

	/// Open a directory in a new Explorer window.
	Q_INVOKABLE void openFolder(const QString& path);

signals:
	/// Sent when an @@open() or @@openSettings() request finished.
	void openFinished(bool success, QString target);

private:
	/// Run a shell handler on a worker thread and report the outcome back on
	/// this object's thread through @@openFinished.
	void runAsync(const QString& target, const QString& verb);
};