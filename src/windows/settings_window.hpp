#pragma once

#include <cwchar>
#include <filesystem>

// clang-format off
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <propkey.h>
// clang-format on

namespace qs::windows::settings {

inline constexpr wchar_t appId[] =
    L"windows.immersivecontrolpanel_cw5n1h2txyewy!microsoft.windows.immersivecontrolpanel";

inline bool isFrame(HWND window) {
	IPropertyStore* store = nullptr;
	if (FAILED(SHGetPropertyStoreForWindow(window, IID_PPV_ARGS(&store)))) return false;
	PROPVARIANT value {};
	const auto result = store->GetValue(PKEY_AppUserModel_ID, &value);
	const bool matches = SUCCEEDED(result) && value.vt == VT_LPWSTR && value.pwszVal
	                  && std::wcscmp(value.pwszVal, appId) == 0;
	PropVariantClear(&value);
	store->Release();
	return matches;
}

inline std::filesystem::path iconPath() {
	wchar_t directory[MAX_PATH] {};
	const auto length = GetWindowsDirectoryW(directory, MAX_PATH);
	if (!length || length >= MAX_PATH) return {};
	return std::filesystem::path(directory) / L"ImmersiveControlPanel" / L"images" / L"logo.png";
}

inline bool centerFrame(HWND frame, HMONITOR monitor) {
	if (IsZoomed(frame) || IsIconic(frame)) return true;
	MONITORINFO info {};
	info.cbSize = sizeof(info);
	RECT rectangle {};
	if (!GetMonitorInfoW(monitor, &info) || !GetWindowRect(frame, &rectangle)) return false;
	const auto width = rectangle.right - rectangle.left;
	const auto height = rectangle.bottom - rectangle.top;
	const auto x = info.rcWork.left + ((info.rcWork.right - info.rcWork.left) - width) / 2;
	const auto y = info.rcWork.top + ((info.rcWork.bottom - info.rcWork.top) - height) / 2;
	return SetWindowPos(frame, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)
	    != FALSE;
}

} // namespace qs::windows::settings
