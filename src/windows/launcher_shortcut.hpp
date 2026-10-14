#pragma once

#include <qabstractnativeeventfilter.h>
#include <qobject.h>
#include <qstring.h>
#include <qwindow.h>
#include <windows.h>

/// Process-owned launcher shortcut, shared across QML reload generations.
class WindowsLauncherShortcut
    : public QObject
    , public QAbstractNativeEventFilter {
	Q_OBJECT;
public:
	explicit WindowsLauncherShortcut(QObject* parent = nullptr);
	~WindowsLauncherShortcut() override;
	static WindowsLauncherShortcut* instance();
	bool available() const { return registered; }
	QString shortcutText() const { return combination; }
	QString errorString() const { return error; }
	QString closeShortcutText() const { return closeCombination; }
	bool nativeEventFilter(const QByteArray& type, void* message, qintptr* result) override;

signals:
	void activated(QString screenName);
	void closeActivated();

private:
	QWindow receiver;
	HWND handle = nullptr;
	bool registered = false;
	QString combination;
	QString error;
	bool closeRegistered = false;
	QString closeCombination;
	HHOOK closeHook = nullptr;
	bool closePressed = false;
	static WindowsLauncherShortcut* hookOwner;
	static LRESULT CALLBACK keyboardProc(int code, WPARAM message, LPARAM data);
};
