// Tests documented Settings launch APIs without starting an immersive host.
// Each invocation must run in a separate process; the caller supplies a timeout
// because native activation can block when the session's shell is unavailable.
// clang-format off
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>
#include <wrl/client.h>
// clang-format on

#include <chrono>
#include <cstdio>
#include <cwchar>

#include "settings_presentation.hpp"

namespace {
bool visibleSettings() {
	const auto windows = qs::windows::test::SettingsSnapshot::read();
	DWORD frameCloak = 1, coreCloak = 1;
	if (windows.frame)
		DwmGetWindowAttribute(windows.frame, DWMWA_CLOAKED, &frameCloak, sizeof(frameCloak));
	if (windows.core)
		DwmGetWindowAttribute(windows.core, DWMWA_CLOAKED, &coreCloak, sizeof(coreCloak));
	return windows.frames == 1 && windows.cores == 1 && GetParent(windows.core) == windows.frame
	    && IsWindowVisible(windows.frame) && IsWindowVisible(windows.core) && !frameCloak
	    && !coreCloak;
}

void describeWindows() {
	const auto windows = qs::windows::test::SettingsSnapshot::read();
	DWORD frameCloak = 99, coreCloak = 99;
	if (windows.frame)
		DwmGetWindowAttribute(windows.frame, DWMWA_CLOAKED, &frameCloak, sizeof(frameCloak));
	if (windows.core)
		DwmGetWindowAttribute(windows.core, DWMWA_CLOAKED, &coreCloak, sizeof(coreCloak));
	DWORD shellPid = 0;
	GetWindowThreadProcessId(GetShellWindow(), &shellPid);
	std::printf(
	    "windows: frames=%u cores=%u attached=%d cloak=%lu/%lu shellPid=%lu\n",
	    windows.frames,
	    windows.cores,
	    windows.core && GetParent(windows.core) == windows.frame,
	    frameCloak,
	    coreCloak,
	    shellPid
	);
}
} // namespace

int wmain(int argc, wchar_t** argv) {
	setvbuf(stdout, nullptr, _IONBF, 0);
	if (argc != 2) {
		std::puts("Usage: windows-settings-api-probe uri|winrt|application|protocol");
		return 2;
	}
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	constexpr auto uri = L"ms-settings:network-wifi";
	std::printf("API: %ls\n", argv[1]);
	describeWindows();
	const bool winrtMode = std::wcscmp(argv[1], L"winrt") == 0;
	const auto apartment = CoInitializeEx(
	    nullptr,
	    winrtMode ? COINIT_MULTITHREADED : COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE
	);
	std::printf("COM: 0x%08lx\n", static_cast<unsigned long>(apartment));
	if (FAILED(apartment)) return 3;
	HRESULT result = E_INVALIDARG;
	DWORD pid = 0;
	if (winrtMode) {
		try {
			// Use the exact documented overload, without package overrides.
			auto operation =
			    winrt::Windows::System::Launcher::LaunchUriAsync(winrt::Windows::Foundation::Uri(uri));
			if (operation.wait_for(std::chrono::seconds(8))
			    == winrt::Windows::Foundation::AsyncStatus::Started)
			{
				operation.Cancel();
				result = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
			} else result = operation.get() ? S_OK : E_FAIL;
		} catch (const winrt::hresult_error& error) {
			result = error.code();
		}
	} else if (std::wcscmp(argv[1], L"uri") == 0) {
		SHELLEXECUTEINFOW info {};
		info.cbSize = sizeof(info);
		info.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
		info.lpVerb = L"open";
		info.lpFile = uri;
		info.nShow = SW_SHOWNORMAL;
		result = ShellExecuteExW(&info) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
	} else {
		Microsoft::WRL::ComPtr<IApplicationActivationManager> manager;
		result = CoCreateInstance(
		    CLSID_ApplicationActivationManager,
		    nullptr,
		    CLSCTX_LOCAL_SERVER,
		    IID_PPV_ARGS(&manager)
		);
		std::printf("Activation manager (local server): 0x%08lx\n", static_cast<unsigned long>(result));
		if (SUCCEEDED(result)) {
			if (std::wcscmp(argv[1], L"application") == 0) {
				// Generic launch has app-specific arguments, not a protocol URI.
				result = manager->ActivateApplication(qs::windows::settings::appId, nullptr, AO_NONE, &pid);
			} else if (std::wcscmp(argv[1], L"protocol") == 0) {
				Microsoft::WRL::ComPtr<IShellItem> item;
				Microsoft::WRL::ComPtr<IShellItemArray> items;
				result = SHCreateItemFromParsingName(uri, nullptr, IID_PPV_ARGS(&item));
				if (SUCCEEDED(result))
					result = SHCreateShellItemArrayFromShellItem(item.Get(), IID_PPV_ARGS(&items));
				if (SUCCEEDED(result))
					result = manager->ActivateForProtocol(qs::windows::settings::appId, items.Get(), &pid);
			}
		}
	}
	std::printf("Activation: 0x%08lx pid=%lu\n", static_cast<unsigned long>(result), pid);
	ULONGLONG stableSince = 0;
	const auto deadline = GetTickCount64() + 5000;
	bool shown = false;
	while (GetTickCount64() < deadline) {
		if (visibleSettings()) {
			if (!stableSince) stableSince = GetTickCount64();
			if (GetTickCount64() - stableSince >= 500) {
				shown = true;
				break;
			}
		} else stableSince = 0;
		qs::windows::test::pumpSettingsMessages(50);
	}
	describeWindows();
	std::printf("RESULT: API=%ls activated=%d visible=%d\n", argv[1], SUCCEEDED(result), shown);
	CoUninitialize();
	return SUCCEEDED(result) && shown ? 0 : 1;
}
