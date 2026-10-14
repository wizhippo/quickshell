#pragma once

#include <QAbstractNativeEventFilter>
#include <QPointer>
#include <windows.h>
#include <shellapi.h>
#include "../window/panelinterface.hpp"
#include "../window/proxywindow.hpp"

class WinPanelInterface;

class WinPanelWindow: public ProxyWindowBase, public QAbstractNativeEventFilter {
	Q_OBJECT;
public:
	explicit WinPanelWindow(WinPanelInterface* owner);
	~WinPanelWindow() override;
	ProxiedWindow* createQQuickWindow() override;
	void connectWindow() override;
	void completeWindow() override;
	ProxiedWindow* disownWindow(bool keepItemOwnership = false) override;
	void setVisibleDirect(bool visible) override;
	void trySetWidth(qint32) override;
	void trySetHeight(qint32) override;
	void setScreen(QuickshellScreenInfo* screen) override;
	bool nativeEventFilter(const QByteArray& type, void* message, qintptr* result) override;
	void schedulePosition();

private:
	APPBARDATA data() const;
	void unregisterBar();
	void positionPanel();
	HMONITOR targetMonitor() const;
	void applyStyles();
	WinPanelInterface* owner;
	HWND hwnd = nullptr;
	UINT callback = 0;
	UINT taskbarCreated = 0;
	bool registered = false;
	bool positioning = false;
	bool pending = false;
	bool notifyingShell = false;
	bool fullscreen = false;
	RECT lastReservation{};
	UINT lastEdge = ABE_TOP;
};

class WinPanelInterface: public PanelWindowInterface {
	Q_OBJECT;
public:
	explicit WinPanelInterface(QObject* parent = nullptr);
	void onReload(QObject* oldInstance) override;
	ProxyWindowBase* proxyWindow() const override;
	Anchors anchors() const override { return mAnchors; }
	void setAnchors(Anchors value) override;
	Margins margins() const override { return mMargins; }
	void setMargins(Margins value) override;
	qint32 exclusiveZone() const override { return mExclusiveZone; }
	void setExclusiveZone(qint32 value) override;
	ExclusionMode::Enum exclusionMode() const override { return mExclusionMode; }
	void setExclusionMode(ExclusionMode::Enum value) override;
	bool aboveWindows() const override { return mAboveWindows; }
	void setAboveWindows(bool value) override;
	bool focusable() const override { return mFocusable; }
	void setFocusable(bool value) override;

private:
	WinPanelWindow* panel;
	Anchors mAnchors;
	Margins mMargins;
	qint32 mExclusiveZone = 0;
	ExclusionMode::Enum mExclusionMode = ExclusionMode::Auto;
	bool mAboveWindows = true;
	bool mFocusable = false;
};
