#include "explorer_session.hpp"
#include <string>

// Restart Manager's SDK header requires Windows types to be defined first.
// clang-format off
#include <windows.h>
#include <restartmanager.h>
// clang-format on

namespace {
std::wstring explorerPath() {
	wchar_t directory[MAX_PATH + 1] {};
	const auto length = GetWindowsDirectoryW(directory, MAX_PATH + 1);
	if (!length || length > MAX_PATH) return {};
	return std::wstring(directory, length) + L"\\explorer.exe";
}

bool isExplorer(HANDLE process) {
	wchar_t path[32768] {};
	DWORD length = 32768;
	const auto expected = explorerPath();
	return !expected.empty() && QueryFullProcessImageNameW(process, 0, path, &length)
	    && _wcsicmp(path, expected.c_str()) == 0;
}

DWORD desktopExplorer() {
	// Only the desktop/taskbar owner, not every file-manager process named Explorer.
	for (const auto window: {GetShellWindow(), FindWindowW(L"Shell_TrayWnd", nullptr)}) {
		DWORD pid = 0;
		if (window) GetWindowThreadProcessId(window, &pid);
		if (!pid) continue;
		const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
		if (!process) continue;
		const bool matches = isExplorer(process);
		CloseHandle(process);
		if (matches) return pid;
	}
	return 0;
}

void showError(const wchar_t* operation, DWORD code) {
	const auto text = std::wstring(operation) + L"\nWindows error: " + std::to_wstring(code);
	MessageBoxW(
	    nullptr,
	    text.c_str(),
	    L"Quickshell - Explorer",
	    MB_OK | MB_ICONERROR | MB_SETFOREGROUND
	);
}

DWORD closeExplorerGracefully(HANDLE process, DWORD pid) {
	// Identify this exact process instance; Restart Manager must not act on a
	// reused PID. Unlike TerminateProcess, this requests a planned shutdown.
	RM_UNIQUE_PROCESS application {};
	application.dwProcessId = pid;
	FILETIME exit {}, kernel {}, user {};
	if (!GetProcessTimes(process, &application.ProcessStartTime, &exit, &kernel, &user))
		return GetLastError();
	DWORD session = 0;
	wchar_t key[CCH_RM_SESSION_KEY + 1] {};
	auto error = RmStartSession(&session, 0, key);
	if (error != ERROR_SUCCESS) return error;
	error = RmRegisterResources(session, 0, nullptr, 1, &application, 0, nullptr);
	if (error == ERROR_SUCCESS) error = RmShutdown(session, 0, nullptr);
	RmEndSession(session);
	return error;
}
} // namespace

bool qs::windows::confirmExplorerReplacement(bool& stoppedExplorer) {
	stoppedExplorer = false;
	const auto pid = desktopExplorer();
	if (!pid) return true;
	const auto answer = MessageBoxW(
	    nullptr,
	    L"Windows Explorer is running. Replace its desktop and taskbar with Quickshell for this "
	    L"session?\n\n"
	    L"Yes asks the desktop Explorer process to exit, including File Explorer windows in that "
	    L"process. "
	    L"Save any file operations first.\n"
	    L"No keeps Explorer running alongside Quickshell. Cancel stops this launch.\n\n"
	    L"Your Windows startup settings will not change.",
	    L"Quickshell - Replace Explorer?",
	    MB_YESNOCANCEL | MB_ICONINFORMATION | MB_DEFBUTTON2 | MB_SETFOREGROUND
	);
	if (answer == IDNO) return true;
	if (answer != IDYES) return false;
	const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
	if (!process) {
		const auto error = GetLastError();
		// Explorer may have exited while the confirmation was open.
		if (!desktopExplorer()) return true;
		showError(L"Could not open the desktop Explorer process. Explorer was left running.", error);
		return false;
	}
	// Recheck identity after the dialog in case the original PID has been reused.
	if (!isExplorer(process)) {
		CloseHandle(process);
		return !desktopExplorer();
	}
	const auto error = closeExplorerGracefully(process, pid);
	stoppedExplorer = desktopExplorer() != pid;
	const auto waited = error == ERROR_SUCCESS ? WaitForSingleObject(process, 5000) : WAIT_FAILED;
	CloseHandle(process);
	if (error != ERROR_SUCCESS || waited != WAIT_OBJECT_0) {
		showError(
		    L"Explorer could not exit cleanly. Quickshell launch was cancelled; Explorer will not be "
		    L"force-killed.",
		    error == ERROR_SUCCESS ? ERROR_TIMEOUT : error
		);
		return false;
	}
	stoppedExplorer = true;
	// Detect an automatic restart before handing shell ownership to QML.
	for (int attempt = 0; attempt < 20; ++attempt) {
		if (desktopExplorer()) {
			showError(
			    L"Explorer restarted during the handoff. Quickshell launch was cancelled.",
			    ERROR_ALREADY_EXISTS
			);
			return false;
		}
		Sleep(100);
	}
	return true;
}

void qs::windows::offerExplorerRestart() {
	if (desktopExplorer()) return; // Already restored; do not create another instance.
	const auto answer = MessageBoxW(
	    nullptr,
	    L"Quickshell has exited. Start Windows Explorer to restore the desktop and taskbar?",
	    L"Quickshell - Start Explorer?",
	    MB_YESNO | MB_ICONINFORMATION | MB_DEFBUTTON2 | MB_SETFOREGROUND
	);
	if (answer != IDYES || desktopExplorer()) return;
	const auto path = explorerPath();
	STARTUPINFOW startup {};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION child {};
	if (path.empty()
	    || !CreateProcessW(
	        path.c_str(),
	        nullptr,
	        nullptr,
	        nullptr,
	        FALSE,
	        0,
	        nullptr,
	        nullptr,
	        &startup,
	        &child
	    ))
	{
		showError(
		    L"Could not start Windows Explorer.",
		    path.empty() ? ERROR_PATH_NOT_FOUND : GetLastError()
		);
		return;
	}
	CloseHandle(child.hThread);
	CloseHandle(child.hProcess);
}
