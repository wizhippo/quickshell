#include <atomic>
#include <chrono>
#include <cstdio>
#include <cwchar>
#include <thread>

#include <windows.h>

#include "../explorer_session.hpp"
#include "../immersive_host.hpp"
#include "settings_presentation.hpp"

namespace {
std::atomic<bool> interrupted {false};
BOOL WINAPI stop(DWORD event) {
	if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT) return FALSE;
	interrupted = true;
	return TRUE;
}

int guardExistingShell() {
	const auto before = GetShellWindow();
	if (!before) return 77; // This test never replaces a desktop shell.
	qs::windows::ImmersiveHost host(L"unused-symbol-cache", [](bool, const std::string&) {});
	if (host.state() != qs::windows::ImmersiveHost::State::Inactive || GetShellWindow() != before)
		return 1;
	std::puts("PASS: existing shell ownership prevents host initialization and symbol downloads.");
	return 0;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
	setvbuf(stdout, nullptr, _IONBF, 0);
	if (argc == 2 && std::wcscmp(argv[1], L"--guard") == 0) return guardExistingShell();
	if (argc != 3
	    || (std::wcscmp(argv[2], L"--host") != 0 && std::wcscmp(argv[2], L"--managed-host") != 0
	        && std::wcscmp(argv[2], L"--check-settings") != 0))
	{
		std::puts(
		    "Usage: windows-immersive-host-probe <symbol-cache> "
		    "--host|--managed-host|--check-settings; or --guard"
		);
		return 2;
	}
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	SetConsoleCtrlHandler(&stop, TRUE);
	const bool checkSettings = std::wcscmp(argv[2], L"--check-settings") == 0;
	if (checkSettings && GetShellWindow()) {
		std::puts(
		    "SKIP: the first-open check requires an absent desktop shell; Explorer is left running."
		);
		return 77;
	}
	bool restoreExplorer = false;
	if (std::wcscmp(argv[2], L"--managed-host") == 0) {
		bool stopped = false;
		if (!qs::windows::confirmExplorerReplacement(stopped)) {
			if (stopped) qs::windows::offerExplorerRestart();
			return 0;
		}
		restoreExplorer = true;
	}
	int result = 0;
	bool settingsChecked = false;
	{
		qs::windows::ImmersiveHost host(argv[1], [](bool warning, const std::string& message) {
			std::fprintf(warning ? stderr : stdout, "%s\n", message.c_str());
		});
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(2);
		while (!interrupted && std::chrono::steady_clock::now() < deadline) {
			if (host.state() == qs::windows::ImmersiveHost::State::Failed
			    || host.state() == qs::windows::ImmersiveHost::State::Inactive)
			{
				result = 1;
				break;
			}
			if (checkSettings && host.state() == qs::windows::ImmersiveHost::State::Ready) {
				settingsChecked = true;
				const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
				if (FAILED(initialized)) result = 1;
				else {
					result = qs::windows::test::checkSettingsPresentation();
					CoUninitialize();
				}
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
	} // Release the controller and registered shell window before restoration.
	if (checkSettings && !settingsChecked) result = 1;
	if (restoreExplorer) qs::windows::offerExplorerRestart();
	return result;
}
