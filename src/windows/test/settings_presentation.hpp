#pragma once

// Optional desktop regression check. The caller owns the native host and has
// already initialized COM. This never replaces Explorer or changes Settings.
#include <cstdio>
#include <cwchar>

// clang-format off
#include <windows.h>
#include <appmodel.h>
#include <dwmapi.h>
#include <shellapi.h>
// clang-format on

#include "../immersive_host.hpp"
#include "../settings_window.hpp"

namespace qs::windows::test {

struct SettingsSnapshot {
	HWND frame = nullptr;
	HWND core = nullptr;
	unsigned frames = 0;
	unsigned cores = 0;
	static BOOL CALLBACK visit(HWND window, LPARAM data) {
		auto& snapshot = *reinterpret_cast<SettingsSnapshot*>(data);
		wchar_t name[128] {};
		GetClassNameW(window, name, 128);
		if (std::wcscmp(name, L"ApplicationFrameWindow") == 0 && settings::isFrame(window)) {
			snapshot.frame = window;
			++snapshot.frames;
		} else if (std::wcscmp(name, L"Windows.UI.Core.CoreWindow") == 0) {
			DWORD pid = 0;
			GetWindowThreadProcessId(window, &pid);
			auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
			if (process) {
				wchar_t appId[1024] {};
				UINT32 length = 1024;
				const auto result = GetApplicationUserModelId(process, &length, appId);
				CloseHandle(process);
				if (result == ERROR_SUCCESS && std::wcscmp(appId, settings::appId) == 0) {
					snapshot.core = window;
					++snapshot.cores;
				}
			}
		}
		return TRUE;
	}
	static BOOL CALLBACK top(HWND window, LPARAM data) {
		visit(window, data);
		EnumChildWindows(window, &visit, data);
		return TRUE;
	}
	static SettingsSnapshot read() {
		SettingsSnapshot snapshot;
		EnumWindows(&top, reinterpret_cast<LPARAM>(&snapshot));
		return snapshot;
	}
	bool ready(bool verbose = false) const {
		if (frames != 1 || cores != 1) {
			if (verbose) std::printf("Settings windows: frames=%u cores=%u\n", frames, cores);
			return false;
		}
		DWORD frameCloak = 1, coreCloak = 1;
		DwmGetWindowAttribute(frame, DWMWA_CLOAKED, &frameCloak, sizeof(frameCloak));
		DwmGetWindowAttribute(core, DWMWA_CLOAKED, &coreCloak, sizeof(coreCloak));
		RECT rectangle {};
		GetWindowRect(frame, &rectangle);
		POINT cursor {};
		GetCursorPos(&cursor);
		MONITORINFO monitor {};
		monitor.cbSize = sizeof(monitor);
		GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &monitor);
		const bool centered = IsZoomed(frame)
		                   || (rectangle.left
		                           == monitor.rcWork.left
		                                  + (monitor.rcWork.right - monitor.rcWork.left
		                                     - (rectangle.right - rectangle.left))
		                                        / 2
		                       && rectangle.top
		                              == monitor.rcWork.top
		                                     + (monitor.rcWork.bottom - monitor.rcWork.top
		                                        - (rectangle.bottom - rectangle.top))
		                                           / 2);
		DWORD_PTR icon = 0;
		SendMessageTimeoutW(frame, WM_GETICON, ICON_SMALL, 0, SMTO_ABORTIFHUNG, 100, &icon);
		GUITHREADINFO input {};
		input.cbSize = sizeof(input);
		GetGUIThreadInfo(GetWindowThreadProcessId(core, nullptr), &input);
		const bool focused = input.hwndFocus == core || IsChild(core, input.hwndFocus);
		const bool foreground = GetAncestor(GetForegroundWindow(), GA_ROOT) == frame;
		const bool attached = GetParent(core) == frame;
		const bool visible = IsWindowVisible(frame) && IsWindowVisible(core) && !IsIconic(frame);
		const bool decorated =
		    (GetWindowLongPtrW(frame, GWL_STYLE) & WS_OVERLAPPEDWINDOW) == WS_OVERLAPPEDWINDOW;
		if (verbose)
			std::printf(
			    "Settings: attached=%d uncloaked=%d visible=%d centered=%d icon=%d "
			    "decorated=%d foreground=%d focus=%d\n",
			    attached,
			    !frameCloak && !coreCloak,
			    visible,
			    centered,
			    !!icon,
			    decorated,
			    foreground,
			    focused
			);
		return attached && !frameCloak && !coreCloak && visible && centered && icon && decorated
		    && foreground && focused;
	}
};

inline void pumpSettingsMessages(unsigned milliseconds) {
	const auto until = GetTickCount64() + milliseconds;
	MSG message {};
	do {
		while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}
		Sleep(10);
	} while (GetTickCount64() < until);
}

inline bool waitForSettings(const char* stage) {
	const auto deadline = GetTickCount64() + 8000;
	ULONGLONG stableSince = 0;
	do {
		if (SettingsSnapshot::read().ready()) {
			if (!stableSince) stableSince = GetTickCount64();
			if (GetTickCount64() - stableSince >= 1000) {
				std::printf("PASS: %s\n", stage);
				SettingsSnapshot::read().ready(true);
				return true;
			}
		} else stableSince = 0;
		pumpSettingsMessages(50);
	} while (GetTickCount64() < deadline);
	std::printf("FAIL: %s\n", stage);
	SettingsSnapshot::read().ready(true);
	return false;
}

inline bool activateSettings(const wchar_t* uri) {
	SHELLEXECUTEINFOW activation {};
	activation.cbSize = sizeof(activation);
	activation.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
	activation.lpFile = uri;
	activation.nShow = SW_SHOWNORMAL;
	if (!ShellExecuteExW(&activation)) {
		std::printf("Settings activation failed: 0x%08lx\n", GetLastError());
		return false;
	}
	ImmersiveHost::requestSettingsActivation();
	return true;
}

inline int checkSettingsPresentation() {
	if (SettingsSnapshot::read().core) {
		std::puts("Close Settings before running the first-open check.");
		return 2;
	}
	if (!activateSettings(L"ms-settings:network-wifi")
	    || !waitForSettings("first Settings activation"))
		return 10;
	auto snapshot = SettingsSnapshot::read();
	ShowWindowAsync(snapshot.frame, SW_MINIMIZE);
	pumpSettingsMessages(500);
	if (!activateSettings(L"ms-settings:powersleep")
	    || !waitForSettings("restore minimized Settings"))
		return 11;
	snapshot = SettingsSnapshot::read();
	// Resizing must keep content fitted after restoring its native parent.
	RECT original {};
	GetWindowRect(snapshot.frame, &original);
	RECT frameBefore {}, coreBefore {};
	GetClientRect(snapshot.frame, &frameBefore);
	GetWindowRect(snapshot.core, &coreBefore);
	MapWindowPoints(nullptr, snapshot.frame, reinterpret_cast<POINT*>(&coreBefore), 2);
	SetWindowPos(
	    snapshot.frame,
	    nullptr,
	    original.left,
	    original.top,
	    original.right - original.left - 80,
	    original.bottom - original.top - 60,
	    SWP_NOZORDER | SWP_NOACTIVATE
	);
	pumpSettingsMessages(500);
	RECT frameClient {}, coreRectangle {};
	GetClientRect(snapshot.frame, &frameClient);
	GetWindowRect(snapshot.core, &coreRectangle);
	MapWindowPoints(nullptr, snapshot.frame, reinterpret_cast<POINT*>(&coreRectangle), 2);
	// The native frame can reserve a one-pixel border; preserve its original insets.
	const bool fitted =
	    coreRectangle.left == coreBefore.left && coreRectangle.top == coreBefore.top
	    && frameClient.right - coreRectangle.right == frameBefore.right - coreBefore.right
	    && frameClient.bottom - coreRectangle.bottom == frameBefore.bottom - coreBefore.bottom;
	std::printf("%s: resized Settings content fits its frame\n", fitted ? "PASS" : "FAIL");
	if (!fitted) return 13;
	PostMessageW(snapshot.frame, WM_CLOSE, 0, 0);
	const auto deadline = GetTickCount64() + 5000;
	while (SettingsSnapshot::read().core && GetTickCount64() < deadline) pumpSettingsMessages(50);
	if (SettingsSnapshot::read().core) return 14;
	if (!activateSettings(L"ms-settings:powersleep") || !waitForSettings("reopen Settings"))
		return 12;
	PostMessageW(SettingsSnapshot::read().frame, WM_CLOSE, 0, 0);
	pumpSettingsMessages(500);
	return 0;
}

} // namespace qs::windows::test
