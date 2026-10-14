#pragma once

#include <qobject.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <windows.h>

#include "../core/model.hpp"

// Experimental shell-side receiver for Windows' undocumented notification protocol.
// The fixed-width wire layout is intentionally separate from NOTIFYICONDATAW.
struct TrayWireIcon {
	DWORD size, window, id, flags, callback, icon;
	WCHAR tip[128];
	DWORD state, stateMask;
	WCHAR info[256];
	DWORD version;
	WCHAR infoTitle[64];
	DWORD infoFlags;
	GUID guid;
	DWORD balloonIcon;
};

class WindowsTrayHost;

///! An application icon in the Windows notification area.
/// Items are registered by applications through the undocumented shell
/// protocol hosted by @@TrayHost, and can only be acquired from it.
class WindowsTrayItem: public QObject {
	Q_OBJECT;
	QML_NAMED_ELEMENT(WindowsTrayItem);
	QML_UNCREATABLE("WindowsTrayItems can only be acquired from TrayHost");
	// clang-format off
	/// Stable identity of the registration, used to match later updates.
	Q_PROPERTY(QString key READ key CONSTANT);
	/// The text the application set as the icon's tooltip.
	Q_PROPERTY(QString tooltip READ tooltip NOTIFY tooltipChanged BINDABLE bindableTooltip);
	/// A `data:` URL PNG of the icon, or an empty string while unavailable.
	Q_PROPERTY(QString iconSource READ iconSource NOTIFY iconSourceChanged BINDABLE bindableIconSource);
	/// True while the application has hidden the icon from the notification area.
	Q_PROPERTY(bool hidden READ hidden NOTIFY hiddenChanged BINDABLE bindableHidden);
	// clang-format on

public:
	explicit WindowsTrayItem(
	    QString key,
	    HWND window,
	    UINT id,
	    GUID guid,
	    bool usesGuid,
	    QObject* parent = nullptr
	);

	[[nodiscard]] QString key() const { return this->mKey; }
	QBindable<QString> bindableTooltip() { return &this->bTooltip; }
	[[nodiscard]] QString tooltip() const { return this->bTooltip; }
	QBindable<QString> bindableIconSource() { return &this->bIconSource; }
	[[nodiscard]] QString iconSource() const { return this->bIconSource; }
	QBindable<bool> bindableHidden() { return &this->bHidden; }
	[[nodiscard]] bool hidden() const { return this->bHidden; }

	/// Relay a click to the application using its negotiated icon version.
	///
	/// `secondary` sends the right-click event, `doubleClick` the double click.
	/// Returns false if the application has no callback or has exited.
	Q_INVOKABLE bool activate(bool secondary = false, bool doubleClick = false);

	/// True while the registering window still exists.
	[[nodiscard]] bool alive() const { return IsWindow(this->mWindow); }

signals:
	void tooltipChanged();
	void iconSourceChanged();
	void hiddenChanged();

public:
	// Native registration state, owned by the item so it survives model churn.
	HWND mWindow = nullptr;
	UINT mId = 0, mCallback = 0, mVersion = 0;
	GUID mGuid {};
	bool mUsesGuid = false;

private:
	friend class WindowsTrayHost;
	bool dispatch(bool secondary, bool doubleClick);

private:
	WindowsTrayHost* mHost;
	QString mKey;
	// clang-format off
	Q_OBJECT_BINDABLE_PROPERTY(WindowsTrayItem, QString, bTooltip, &WindowsTrayItem::tooltipChanged);
	Q_OBJECT_BINDABLE_PROPERTY(WindowsTrayItem, QString, bIconSource, &WindowsTrayItem::iconSourceChanged);
	Q_OBJECT_BINDABLE_PROPERTY(WindowsTrayItem, bool, bHidden, &WindowsTrayItem::hiddenChanged);
	// clang-format on
};

/// Process-owned host for Windows' notification area protocol.
///
/// The receiver window outlives QML reloads, so this is a process singleton.
/// `Quickshell.Windows.TrayHost` is the per-engine QML facade over it.
class WindowsTrayHost: public QObject {
	Q_OBJECT;

public:
	explicit WindowsTrayHost(QObject* parent = nullptr);
	~WindowsTrayHost() override;

	static WindowsTrayHost* instance();

	[[nodiscard]] ObjectModel<WindowsTrayItem>* items() { return &this->mItems; }
	[[nodiscard]] bool available() const { return this->trayWindow && IsWindow(this->trayWindow); }
	[[nodiscard]] QString errorString() const { return this->error; }

	/// Start our tray receiver if no other shell currently hosts the tray.
	bool start();

	/// Move a menu an application just opened to the last tray click, if the
	/// application placed it far from there. Called from the process menu watch.
	void pullMenuToAnchor(HWND menu);

	[[nodiscard]] WindowsTrayItem* find(const QString& key) const;

signals:
	void statusChanged();

private:
	friend class WindowsTrayItem;
	static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
	bool receive(DWORD operation, const TrayWireIcon& packet);
	bool dispatch(WindowsTrayItem* item, bool secondary, bool doubleClick);
	void loseTrayWindow();
	void prune();

	ObjectModel<WindowsTrayItem> mItems {this};
	QTimer cleanup;
	HWND trayWindow = nullptr;
	HWND notifyWindow = nullptr;
	HWINEVENTHOOK menuWatch = nullptr;
	// Where the user clicked, so a mispositioned menu can be corrected.
	POINT anchor {};
	DWORD anchorTick = 0;
	QString error;
};