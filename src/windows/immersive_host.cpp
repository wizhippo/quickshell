// Native Windows Settings hosting. Private ABIs are checked against matching
// Microsoft symbols and the two Windows DLL identities verified by this port.
// clang-format off
#include <windows.h>
#include <dbghelp.h>
#include <objbase.h>
#include <servprov.h>
#include <inspectable.h>
#include <shobjidl.h>
#include <propkey.h>
#include <dwmapi.h>
#include <appmodel.h>
#include <shellapi.h>
// clang-format on

#include <array>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <winhttp.h>

#include "immersive_host.hpp"
#include "settings_window.hpp"

namespace {
thread_local const qs::windows::ImmersiveHost::Logger* hostLogger = nullptr;
std::atomic<ULONGLONG> settingsActivationDeadline {0};
std::atomic<unsigned> settingsActivationGeneration {0};
void report(bool warning, const char* format, ...) {
	char text[4096] {};
	va_list arguments;
	va_start(arguments, format);
	std::vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	if (hostLogger) (*hostLogger)(warning, text);
}

void require(bool ok, const char* operation) {
	if (!ok)
		throw std::runtime_error(
		    std::string(operation) + " failed (" + std::to_string(GetLastError()) + ")"
		);
}

void check(HRESULT result, const char* operation) {
	if (FAILED(result)) {
		char code[16] {};
		std::snprintf(code, sizeof(code), "0x%08lx", static_cast<unsigned long>(result));
		throw std::runtime_error(std::string(operation) + " failed (" + code + ")");
	}
}

struct Apartment {
	Apartment() { check(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "COM STA"); }
	~Apartment() { CoUninitialize(); }
};

struct Module {
	HMODULE handle;
	explicit Module(const wchar_t* name)
	    : handle(LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {
		require(handle != nullptr, "LoadLibraryExW");
	}
	~Module() { FreeLibrary(handle); }
	Module(const Module&) = delete;
	Module& operator=(const Module&) = delete;
};

struct ComOwner {
	IUnknown* value = nullptr;
	~ComOwner() {
		if (value) value->Release();
	}
	ComOwner() = default;
	ComOwner(const ComOwner&) = delete;
	ComOwner& operator=(const ComOwner&) = delete;
};

struct CodeView {
	GUID signature;
	DWORD age;
	std::string name;
};

CodeView codeView(HMODULE module) {
	const auto* base = reinterpret_cast<const unsigned char*>(module);
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
	require(dos->e_magic == IMAGE_DOS_SIGNATURE && dos->e_lfanew > 0, "DOS header");
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
	require(nt->Signature == IMAGE_NT_SIGNATURE, "PE header");
	const auto imageSize = nt->OptionalHeader.SizeOfImage;
	const auto& debug = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
	require(
	    debug.VirtualAddress < imageSize && debug.Size <= imageSize - debug.VirtualAddress,
	    "debug directory bounds"
	);
	const auto* records = reinterpret_cast<const IMAGE_DEBUG_DIRECTORY*>(base + debug.VirtualAddress);
	for (size_t i = 0; i < debug.Size / sizeof(*records); ++i) {
		const auto& record = records[i];
		if (record.Type != IMAGE_DEBUG_TYPE_CODEVIEW || record.SizeOfData < 25) continue;
		require(
		    record.AddressOfRawData < imageSize
		        && record.SizeOfData <= imageSize - record.AddressOfRawData,
		    "CodeView bounds"
		);
		const auto* data = base + record.AddressOfRawData;
		if (std::memcmp(data, "RSDS", 4) != 0) continue;
		CodeView result {};
		std::memcpy(&result.signature, data + 4, sizeof(GUID));
		std::memcpy(&result.age, data + 20, sizeof(DWORD));
		const auto* name = reinterpret_cast<const char*>(data + 24);
		const auto* end = static_cast<const char*>(std::memchr(name, 0, record.SizeOfData - 24));
		require(end != nullptr, "CodeView name");
		result.name = std::filesystem::path(std::string(name, end)).filename().string();
		return result;
	}
	throw std::runtime_error("No RSDS debug identity in installed module");
}

std::string symbolKey(const CodeView& cv) {
	char key[64];
	const auto& g = cv.signature;
	std::snprintf(
	    key,
	    sizeof(key),
	    "%08lX%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X%lX",
	    g.Data1,
	    g.Data2,
	    g.Data3,
	    g.Data4[0],
	    g.Data4[1],
	    g.Data4[2],
	    g.Data4[3],
	    g.Data4[4],
	    g.Data4[5],
	    g.Data4[6],
	    g.Data4[7],
	    cv.age
	);
	return key;
}

struct InternetHandle {
	HINTERNET value = nullptr;
	explicit InternetHandle(HINTERNET value): value(value) {
		require(value != nullptr, "Windows symbol HTTP handle");
	}
	~InternetHandle() { WinHttpCloseHandle(value); }
	InternetHandle(const InternetHandle&) = delete;
	InternetHandle& operator=(const InternetHandle&) = delete;
};

void ensurePdb(
    const std::filesystem::path& file,
    const CodeView& cv,
    const std::atomic<bool>* cancelled
) {
	if (std::filesystem::is_regular_file(file)) return;
	report(false, "Fetching matching Microsoft symbols for %s", cv.name.c_str());
	std::filesystem::create_directories(file.parent_path());
	InternetHandle session(WinHttpOpen(
	    L"Quickshell Windows Settings host",
	    WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
	    nullptr,
	    nullptr,
	    0
	));
	require(WinHttpSetTimeouts(session.value, 5000, 5000, 5000, 5000), "symbol HTTP timeouts");
	InternetHandle connection(
	    WinHttpConnect(session.value, L"msdl.microsoft.com", INTERNET_DEFAULT_HTTPS_PORT, 0)
	);
	const auto path = "/download/symbols/" + cv.name + "/" + symbolKey(cv) + "/" + cv.name;
	const std::wstring requestPath(path.begin(), path.end());
	InternetHandle request(WinHttpOpenRequest(
	    connection.value,
	    L"GET",
	    requestPath.c_str(),
	    nullptr,
	    nullptr,
	    nullptr,
	    WINHTTP_FLAG_SECURE
	));
	require(
	    WinHttpSendRequest(request.value, nullptr, 0, nullptr, 0, 0, 0),
	    "request Microsoft symbols"
	);
	require(WinHttpReceiveResponse(request.value, nullptr), "receive Microsoft symbols");
	DWORD status = 0, size = sizeof(status);
	require(
	    WinHttpQueryHeaders(
	        request.value,
	        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
	        nullptr,
	        &status,
	        &size,
	        nullptr
	    ) && status == 200,
	    "Microsoft symbol server status"
	);
	auto temporary = file;
	temporary += L"." + std::to_wstring(GetCurrentProcessId()) + L".download";
	struct RemovePartial {
		std::filesystem::path path;
		~RemovePartial() {
			std::error_code ignored;
			std::filesystem::remove(path, ignored);
		}
	} cleanup {temporary};
	std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
	require(static_cast<bool>(output), "open symbol cache file");
	const auto began = std::chrono::steady_clock::now();
	std::array<char, 64 * 1024> bytes {};
	size_t total = 0;
	for (;;) {
		if ((cancelled && cancelled->load())
		    || std::chrono::steady_clock::now() - began > std::chrono::seconds(60))
			throw std::runtime_error("Windows symbol download cancelled or timed out");
		DWORD received = 0;
		require(
		    WinHttpReadData(request.value, bytes.data(), static_cast<DWORD>(bytes.size()), &received),
		    "read Microsoft symbols"
		);
		if (!received) break;
		total += received;
		require(total <= 128 * 1024 * 1024, "symbol download size limit");
		output.write(bytes.data(), received);
		require(static_cast<bool>(output), "write symbol cache");
	}
	output.close();
	require(total > 0 && static_cast<bool>(output), "complete symbol download");
	require(
	    MoveFileExW(
	        temporary.c_str(),
	        file.c_str(),
	        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
	    ),
	    "publish symbol cache"
	);
}

class Symbols {
public:
	explicit Symbols(const std::filesystem::path& cache, const std::atomic<bool>* cancelled = nullptr)
	    : cancelled(cancelled) {
		SymSetOptions(SYMOPT_EXACT_SYMBOLS | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
		require(SymInitializeW(GetCurrentProcess(), cache.c_str(), FALSE), "SymInitializeW");
	}
	~Symbols() { SymCleanup(GetCurrentProcess()); }

	void load(HMODULE module, const std::filesystem::path& cache) {
		const auto cv = codeView(module);
		const auto pdb = cache / cv.name / symbolKey(cv) / cv.name;
		ensurePdb(pdb, cv, cancelled);
		// Load the PDB directly at the DLL's actual base. DbgHelp is confined to
		// this host's STA; no background symbol calls or symbol downloads.
		const auto address = reinterpret_cast<DWORD64>(module);
		require(
		    SymLoadModuleExW(GetCurrentProcess(), nullptr, pdb.c_str(), nullptr, address, 0, nullptr, 0)
		        != 0,
		    "SymLoadModuleExW"
		);
		IMAGEHLP_MODULEW64 info {};
		info.SizeOfStruct = sizeof(info);
		require(SymGetModuleInfoW64(GetCurrentProcess(), address, &info), "SymGetModuleInfoW64");
		require(
		    info.SymType == SymPdb && !info.PdbUnmatched && info.PdbAge == cv.age
		        && IsEqualGUID(info.PdbSig70, cv.signature),
		    "PDB identity verification"
		);
		report(
		    false,
		    "PDB verified: %s / %s; type information=%d\n",
		    cv.name.c_str(),
		    symbolKey(cv).c_str(),
		    info.TypeInfo
		);
	}

	uintptr_t address(HMODULE module, const char* name) {
		alignas(SYMBOL_INFO) std::array<unsigned char, sizeof(SYMBOL_INFO) + MAX_SYM_NAME> storage {};
		auto* info = reinterpret_cast<SYMBOL_INFO*>(storage.data());
		info->SizeOfStruct = sizeof(*info);
		info->MaxNameLen = MAX_SYM_NAME;
		require(SymFromName(GetCurrentProcess(), name, info), name);
		require(info->ModBase == reinterpret_cast<DWORD64>(module), "symbol module ownership");
		return static_cast<uintptr_t>(info->Address);
	}
	const std::atomic<bool>* cancelled;
};

using BandCall = HWND(WINAPI*)(
    DWORD,
    LPCWSTR,
    LPCWSTR,
    DWORD,
    int,
    int,
    int,
    int,
    HWND,
    HMENU,
    HINSTANCE,
    LPVOID,
    DWORD
);
BandCall nativeBandCall = nullptr;
HWND WINAPI desktopBand(
    DWORD ex,
    LPCWSTR cls,
    LPCWSTR title,
    DWORD style,
    int x,
    int y,
    int w,
    int h,
    HWND parent,
    HMENU menu,
    HINSTANCE instance,
    LPVOID param,
    DWORD band
) {
	const auto window = nativeBandCall(
	    ex,
	    cls,
	    title,
	    style,
	    x,
	    y,
	    w,
	    h,
	    parent,
	    menu,
	    instance,
	    param,
	    band == 12 ? 1 : band
	);
	if (band == 12)
		report(
		    false,
		    "Fallback desktop-band window: %p, error=%lu\n",
		    window,
		    window ? 0 : GetLastError()
		);
	return window;
}

class BandImport {
public:
	BandImport(Symbols& symbols, HMODULE module) {
		nativeBandCall = reinterpret_cast<BandCall>(
		    GetProcAddress(GetModuleHandleW(L"user32.dll"), "CreateWindowInBand")
		);
		require(nativeBandCall != nullptr, "CreateWindowInBand export");
		this->slot = reinterpret_cast<void**>(symbols.address(module, "__imp_CreateWindowInBand"));
		this->original = *this->slot;
		const auto thunk = symbols.address(module, "__imp_load_CreateWindowInBand");
		require(
		    original == reinterpret_cast<void*>(nativeBandCall)
		        || original == reinterpret_cast<void*>(thunk),
		    "band import target"
		);
		write(reinterpret_cast<void*>(&desktopBand));
	}
	~BandImport() {
		try {
			write(original);
		} catch (const std::exception& error) {
			report(true, "Import cleanup: %s\n", error.what());
		}
	}

private:
	void write(void* value) {
		DWORD protection = 0;
		require(VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection), "import protection");
		const auto previous = InterlockedExchangePointer(slot, value);
		DWORD ignored = 0;
		if (!VirtualProtect(slot, sizeof(*slot), protection, &ignored)) {
			InterlockedExchangePointer(slot, previous);
			VirtualProtect(slot, sizeof(*slot), protection, &ignored);
			throw std::runtime_error("Could not restore immersive import page protection");
		}
	}
	void** slot = nullptr;
	void* original = nullptr;
};

struct ShellWindow {
	HWND window = nullptr;
	ShellWindow() {
		require(GetShellWindow() == nullptr, "another shell is registered; leave it alone");
		auto registerShell = reinterpret_cast<BOOL(WINAPI*)(HWND, HWND)>(
		    GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetShellWindowEx")
		);
		require(registerShell != nullptr, "SetShellWindowEx export");
		window = CreateWindowExW(
		    0,
		    L"STATIC",
		    L"Quickshell Windows Settings host",
		    WS_POPUP,
		    0,
		    0,
		    1,
		    1,
		    nullptr,
		    nullptr,
		    GetModuleHandleW(nullptr),
		    nullptr
		);
		require(window != nullptr, "CreateWindowExW");
		if (!registerShell(window, nullptr) || GetShellWindow() != window) {
			DestroyWindow(window);
			window = nullptr;
			throw std::runtime_error("Shell window registration failed");
		}
	}
	~ShellWindow() {
		if (window) DestroyWindow(window);
	}
};

constexpr GUID
    builderClass {0xc71c41f1, 0xddad, 0x42dc, {0xa8, 0xfc, 0xf5, 0xbf, 0xc6, 0x1d, 0xf9, 0x57}};
constexpr const char* createName = "?CreateImmersiveShellController@CImmersiveShellBuilder@@"
                                   "UEAAJPEAPEAUIImmersiveShellController@@@Z";
constexpr const char* startName = "?Start@CImmersiveShellController@@UEAAJXZ";
constexpr const char* behaviorVtableName =
    "??_7CImmersiveShellCreationBehavior@@6B?$Selector@U?$ImplementsHelper@U?$RuntimeClassFlags@$"
    "01@WRL@Microsoft@@$00UIImmersiveShellCreationBehavior@@@Details@WRL@Microsoft@@U?$"
    "ImplementsHelper@U?$RuntimeClassFlags@$01@WRL@Microsoft@@$0A@U?$ImplementsMarker@VFtmBase@WRL@"
    "Microsoft@@@Details@23@UIImmersiveShellCreationBehavior@@@234@@Details@WRL@Microsoft@@@";

// Method order is checked against the matching PDB before using this ABI.
struct CreationBehavior: IUnknown {
	virtual HRESULT STDMETHODCALLTYPE threadStart() = 0;
	virtual HRESULT STDMETHODCALLTYPE threadStop() = 0;
	virtual HRESULT STDMETHODCALLTYPE shellCreated(IUnknown*) = 0;
	virtual HRESULT STDMETHODCALLTYPE maximumCount(UINT*) = 0;
	virtual HRESULT STDMETHODCALLTYPE createComponent(UINT, REFIID, void**) = 0;
	virtual HRESULT STDMETHODCALLTYPE shouldCreate(UINT, BOOL*, GUID*) = 0;
	virtual HRESULT STDMETHODCALLTYPE monitorCount(int, UINT*) = 0;
	virtual HRESULT STDMETHODCALLTYPE createMonitorComponent(int, UINT, REFIID, void**) = 0;
	virtual HRESULT STDMETHODCALLTYPE shouldCreateMonitor(int, UINT, BOOL*) = 0;
};

class ComponentPolicy final: public CreationBehavior {
public:
	explicit ComponentPolicy(CreationBehavior* native): native(native) {
		native->AddRef();
		const auto result = CoCreateFreeThreadedMarshaler(this, &marshaler);
		if (FAILED(result)) {
			native->Release();
			check(result, "Create policy marshaler");
		}
	}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** output) override {
		if (!output) return E_POINTER;
		*output = nullptr;
		if (IsEqualIID(id, __uuidof(IUnknown)) || IsEqualIID(id, __uuidof(IAgileObject))) {
			*output = static_cast<CreationBehavior*>(this);
			AddRef();
			return S_OK;
		}
		if (IsEqualIID(id, __uuidof(IMarshal))) return marshaler->QueryInterface(id, output);
		IUnknown* supported = nullptr;
		const auto result = native->QueryInterface(id, reinterpret_cast<void**>(&supported));
		if (FAILED(result)) return result;
		if (supported == static_cast<IUnknown*>(native)) {
			supported->Release();
			*output = static_cast<CreationBehavior*>(this);
			AddRef();
		} else *output = supported;
		return S_OK;
	}
	ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
	ULONG STDMETHODCALLTYPE Release() override {
		const auto left = --references;
		if (!left) delete this;
		return left;
	}
	HRESULT STDMETHODCALLTYPE threadStart() override {
		const auto result = native->threadStart();
		report(false, "Native behavior thread start: 0x%08lx\n", static_cast<unsigned long>(result));
		return result;
	}
	HRESULT STDMETHODCALLTYPE threadStop() override {
		const auto result = native->threadStop();
		report(false, "Native behavior thread stop: 0x%08lx\n", static_cast<unsigned long>(result));
		return result;
	}
	HRESULT STDMETHODCALLTYPE shellCreated(IUnknown* shell) override {
		const auto result = native->shellCreated(shell);
		report(false, "Native behavior shell created: 0x%08lx\n", static_cast<unsigned long>(result));
		return result;
	}
	HRESULT STDMETHODCALLTYPE maximumCount(UINT* count) override {
		return native->maximumCount(count);
	}
	HRESULT STDMETHODCALLTYPE createComponent(UINT index, REFIID id, void** output) override {
		const auto result = native->createComponent(index, id, output);
		return result;
	}
	HRESULT STDMETHODCALLTYPE shouldCreate(UINT index, BOOL* enabled, GUID* id) override {
		const auto result = native->shouldCreate(index, enabled, id);
		constexpr GUID
		    keyboard {0xf580a09b, 0xea6e, 0x4d83, {0x9a, 0x03, 0xad, 0xd6, 0xcb, 0x75, 0x6a, 0xb3}};
		if (SUCCEEDED(result) && IsEqualGUID(*id, keyboard)) {
			*enabled = FALSE;
			report(false, "Skipping touch-keyboard shell component %u in the Settings host\n", index);
		}
		return result;
	}
	HRESULT STDMETHODCALLTYPE monitorCount(int monitor, UINT* count) override {
		return native->monitorCount(monitor, count);
	}
	HRESULT STDMETHODCALLTYPE
	createMonitorComponent(int monitor, UINT index, REFIID id, void** output) override {
		return native->createMonitorComponent(monitor, index, id, output);
	}
	HRESULT STDMETHODCALLTYPE shouldCreateMonitor(int monitor, UINT index, BOOL* enabled) override {
		return native->shouldCreateMonitor(monitor, index, enabled);
	}

private:
	~ComponentPolicy() {
		marshaler->Release();
		native->Release();
	}
	std::atomic<ULONG> references {1};
	CreationBehavior* native;
	IUnknown* marshaler = nullptr;
};

void setComponentPolicy(Symbols& symbols, HMODULE twinui, HMODULE provider, IUnknown* controller) {
	const auto* table =
	    reinterpret_cast<const uintptr_t*>(symbols.address(twinui, behaviorVtableName));
	constexpr const char* names[] {
	    "?OnImmersiveShellThreadStart@CImmersiveShellCreationBehavior@@UEAAJXZ",
	    "?OnImmersiveShellThreadStop@CImmersiveShellCreationBehavior@@UEAAJXZ",
	    "?OnImmersiveShellCreated@CImmersiveShellCreationBehavior@@UEAAJPEAUIUnknown@@@Z",
	    "?GetMaximumComponentCount@CImmersiveShellCreationBehavior@@UEAAJPEAI@Z",
	    "?CreateComponent@CImmersiveShellCreationBehavior@@UEAAJIAEBU_GUID@@PEAPEAX@Z",
	    "?ShouldCreateComponent@CImmersiveShellCreationBehavior@@UEAAJIPEAHPEAU_GUID@@@Z",
	    "?GetPerMonitorComponentCount@CImmersiveShellCreationBehavior@@UEAAJHPEAI@Z",
	    "?CreatePerMonitorComponent@CImmersiveShellCreationBehavior@@UEAAJHIAEBU_GUID@@PEAPEAX@Z",
	    "?ShouldCreatePerMonitorComponent@CImmersiveShellCreationBehavior@@UEAAJHIPEAH@Z",
	};
	for (size_t i = 0; i < std::size(names); ++i)
		require(table[i + 3] == symbols.address(twinui, names[i]), "creation behavior ABI");
	const auto make = symbols.address(
	    twinui,
	    "??$Make@VCImmersiveShellCreationBehavior@@$$V@Details@WRL@Microsoft@@YA?AV?$ComPtr@"
	    "VCImmersiveShellCreationBehavior@@@12@XZ"
	);
	ComOwner object;
	// MSVC x64 returns this nontrivial, single-pointer ComPtr using caller
	// storage. The installed function's signature and code were inspected.
	reinterpret_cast<void* (*) (IUnknown**)>(make)(&object.value);
	require(object.value != nullptr, "default creation behavior factory");
	CreationBehavior* native = nullptr;
	// Find the named interface in the new object rather than assuming a member
	// or interface offset. Only inspect the first 64 bytes through the OS.
	std::array<uintptr_t, 8> interfaces {};
	SIZE_T read = 0;
	require(
	    ReadProcessMemory(
	        GetCurrentProcess(),
	        object.value,
	        interfaces.data(),
	        sizeof(interfaces),
	        &read
	    ) && read == sizeof(interfaces),
	    "creation behavior interface inspection"
	);
	for (size_t i = 0; i < interfaces.size(); ++i) {
		if (interfaces[i] == reinterpret_cast<uintptr_t>(table)) {
			native = reinterpret_cast<CreationBehavior*>(
			    reinterpret_cast<unsigned char*>(object.value) + i * sizeof(uintptr_t)
			);
			break;
		}
	}
	require(native != nullptr, "default creation behavior interface");
	ComOwner policy;
	policy.value = new ComponentPolicy(native);
	const auto setter = (*reinterpret_cast<uintptr_t**>(controller))[5];
	require(
	    setter
	        == symbols.address(
	            provider,
	            "?SetCreationBehavior@CImmersiveShellController@@"
	            "UEAAJPEAUIImmersiveShellCreationBehavior@@@Z"
	        ),
	    "creation policy setter ABI"
	);
	check(
	    reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IUnknown*, IUnknown*)>(setter)(
	        controller,
	        policy.value
	    ),
	    "Set native component policy"
	);
}

bool readPointers(const void* from, uintptr_t* to, size_t count) {
	SIZE_T read = 0;
	return ReadProcessMemory(GetCurrentProcess(), from, to, count * sizeof(uintptr_t), &read)
	    && read == count * sizeof(uintptr_t);
}

// Returns a retained interface only when the specified method slot matches the
// requested method in the installed DLL. No private member offsets are used.
IUnknown* firstMethodInterface(IUnknown* object, uintptr_t wanted, size_t slot = 3) {
	ComOwner inspectable;
	if (FAILED(object->QueryInterface(
	        __uuidof(IInspectable),
	        reinterpret_cast<void**>(&inspectable.value)
	    )))
		return nullptr;
	ULONG count = 0;
	IID* ids = nullptr;
	if (FAILED(static_cast<IInspectable*>(inspectable.value)->GetIids(&count, &ids))) return nullptr;
	IUnknown* found = nullptr;
	for (ULONG i = 0; i < count; ++i) {
		IUnknown* candidate = nullptr;
		if (FAILED(object->QueryInterface(ids[i], reinterpret_cast<void**>(&candidate)))) continue;
		uintptr_t table = 0;
		std::vector<uintptr_t> methods(slot + 1);
		if (readPointers(candidate, &table, 1)
		    && readPointers(reinterpret_cast<void*>(table), methods.data(), methods.size())
		    && methods[slot] == wanted)
		{
			found = candidate;
			break;
		}
		candidate->Release();
	}
	CoTaskMemFree(ids);
	return found;
}

bool settingsCore(HWND window) {
	DWORD pid = 0;
	GetWindowThreadProcessId(window, &pid);
	const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (!process) return false;
	UINT32 count = 1024;
	wchar_t id[1024] {};
	const auto result = GetApplicationUserModelId(process, &count, id);
	CloseHandle(process);
	return result == ERROR_SUCCESS && std::wcscmp(id, qs::windows::settings::appId) == 0;
}

struct SettingsWindows {
	std::vector<HWND> frames;
	std::vector<HWND> cores;
	static BOOL CALLBACK visit(HWND window, LPARAM data) {
		auto* snapshot = reinterpret_cast<SettingsWindows*>(data);
		wchar_t name[128] {};
		GetClassNameW(window, name, 128);
		if (std::wcscmp(name, L"ApplicationFrameWindow") == 0 && qs::windows::settings::isFrame(window))
			snapshot->frames.push_back(window);
		if (std::wcscmp(name, L"Windows.UI.Core.CoreWindow") == 0 && settingsCore(window))
			snapshot->cores.push_back(window);
		return TRUE;
	}
	static BOOL CALLBACK top(HWND window, LPARAM data) {
		visit(window, data);
		EnumChildWindows(window, &visit, data);
		return TRUE;
	}
};

class SettingsPresentation {
public:
	~SettingsPresentation() {
		// The frame belongs to another process; do not leave it referencing freed icons.
		if (IsWindow(iconFrame)) {
			if (reinterpret_cast<HICON>(SendMessageW(iconFrame, WM_GETICON, ICON_BIG, 0)) == largeIcon)
				SendMessageW(iconFrame, WM_SETICON, ICON_BIG, 0);
			if (reinterpret_cast<HICON>(SendMessageW(iconFrame, WM_GETICON, ICON_SMALL, 0)) == smallIcon)
				SendMessageW(iconFrame, WM_SETICON, ICON_SMALL, 0);
		}
		if (largeIcon) DestroyIcon(largeIcon);
		if (smallIcon) DestroyIcon(smallIcon);
	}

	SettingsPresentation(Symbols& symbols, HMODULE twinui): symbols(symbols), module(twinui) {
		constexpr GUID
		    shellClass {0xc2f03a33, 0x21f5, 0x47fa, {0xb4, 0xbb, 0x15, 0x63, 0x62, 0xa2, 0xf2, 0x39}};
		ComOwner provider;
		check(
		    CoCreateInstance(
		        shellClass,
		        nullptr,
		        CLSCTX_LOCAL_SERVER,
		        __uuidof(IServiceProvider),
		        reinterpret_cast<void**>(&provider.value)
		    ),
		    "Get presentation services"
		);
		const auto& id = *reinterpret_cast<const GUID*>(
		    symbols.address(twinui, "?SID_ViewEventDispatcher@@3U_GUID@@B")
		);
		check(
		    static_cast<IServiceProvider*>(provider.value)
		        ->QueryService(id, __uuidof(IUnknown), reinterpret_cast<void**>(&dispatcher.value)),
		    "Get view dispatcher"
		);
		constexpr GUID applicationCollectionId {
		    0x1841c6d7,
		    0x4f9d,
		    0x42c0,
		    {0xaf, 0x41, 0x87, 0x47, 0x53, 0x8f, 0x10, 0xe5}
		};
		check(
		    static_cast<IServiceProvider*>(provider.value)
		        ->QueryService(
		            applicationCollectionId,
		            applicationCollectionId,
		            reinterpret_cast<void**>(&applications.value)
		        ),
		    "Get application views"
		);

		getViews = symbols.address(
		    module,
		    "?GetViews@UwpWindowEventDispatcher@@UEAAXAEAV?$vector@V?$ComPtr@UIViewWrapper@"
		    "ViewManagerInterop@Shell@Internal@Windows@@@WRL@Microsoft@@V?$allocator@V?$ComPtr@"
		    "UIViewWrapper@ViewManagerInterop@Shell@Internal@Windows@@@WRL@Microsoft@@@std@@@std@@@Z"
		);
		collection.value = firstMethodInterface(dispatcher.value, getViews, 6);
		require(collection.value != nullptr, "UWP view enumeration interface");
		destroyViews = symbols.address(
		    module,
		    "??1?$vector@V?$ComPtr@UIViewWrapper@ViewManagerInterop@Shell@Internal@Windows@@@WRL@"
		    "Microsoft@@V?$allocator@V?$ComPtr@UIViewWrapper@ViewManagerInterop@Shell@Internal@Windows@"
		    "@@WRL@Microsoft@@@std@@@std@@QEAA@XZ"
		);
	}

	void tick() {
		SettingsWindows windows;
		EnumWindows(&SettingsWindows::top, reinterpret_cast<LPARAM>(&windows));
		if (windows.frames.empty()) {
			completedFrame = nullptr;
			confirmingFrame = nullptr;
		}
		if (windows.cores.size() != 1 || windows.frames.size() != 1) return;
		const auto frame = windows.frames.front();
		const auto core = windows.cores.front();
		DWORD pid = 0;
		GetWindowThreadProcessId(core, &pid);
		const auto generation = settingsActivationGeneration.load();
		const bool activationRequested =
		    generation != activatedGeneration && GetTickCount64() < settingsActivationDeadline.load();
		if (IsIconic(frame)) {
			if (activationRequested) ShowWindowAsync(frame, SW_RESTORE);
			return;
		}
		// GetViewFromHwnd uses a map that is not populated yet on first launch.
		// Match the frame already owned by the dispatcher instead.
		// The pinned Windows DLL uses a release MSVC vector (three pointers).
		// Allocation and destruction both remain in that DLL, independent of
		// our STL allocator and debug iterator configuration.
		struct NativeViews {
			IUnknown** begin = nullptr;
			IUnknown** end = nullptr;
			IUnknown** capacity = nullptr;
		};
		NativeViews views;
		struct ReleaseViews {
			NativeViews& views;
			uintptr_t destroy;
			~ReleaseViews() { reinterpret_cast<void (*)(NativeViews*)>(destroy)(&views); }
		} releaseViews {views, destroyViews};
		reinterpret_cast<void(STDMETHODCALLTYPE*)(IUnknown*, NativeViews&)>(getViews)(
		    collection.value,
		    views
		);
		ComOwner wrapper;
		const auto frameMethod =
		    symbols.address(module, "?GetFrameHwnd@UwpWindowWrapperBase@@UEAAPEAUHWND__@@XZ");
		for (auto entry = views.begin; entry != views.end; ++entry) {
			ComOwner framed;
			framed.value = firstMethodInterface(*entry, frameMethod, 44);
			if (framed.value
			    && reinterpret_cast<HWND(STDMETHODCALLTYPE*)(IUnknown*)>(frameMethod)(framed.value)
			           == frame)
			{
				wrapper.value = framed.value;
				wrapper.value->AddRef();
				break;
			}
		}
		if (!wrapper.value) return;
		ComOwner view;
		const auto discoveryAddress = symbols.address(
		    module,
		    "?WindowDiscoveredFromShellHook@UwpWindowWrapperBase@@UEAAXPEAUHWND__@@@Z"
		);
		view.value = firstMethodInterface(wrapper.value, discoveryAddress, 24);
		require(view.value != nullptr, "view discovery interface");
		uintptr_t table = 0;
		std::array<uintptr_t, 42> methods {};
		require(
		    readPointers(view.value, &table, 1)
		        && readPointers(reinterpret_cast<void*>(table), methods.data(), methods.size()),
		    "view methods read"
		);
		require(
		    methods[24]
		        == symbols.address(
		            module,
		            "?WindowDiscoveredFromShellHook@UwpWindowWrapperBase@@UEAAXPEAUHWND__@@@Z"
		        ),
		    "view discovery ABI"
		);
		require(
		    methods[40]
		        == symbols.address(
		            module,
		            "?VisibilityChanged@UwpWindowWrapperBase@@UEAAXW4EventPhase@@W4Visibility@@@Z"
		        ),
		    "view visibility ABI"
		);
		require(
		    methods[41]
		        == symbols.address(
		            module,
		            "?HandlePresentationReadinessChange@UwpWindowWrapperBase@@UEAAX_K@Z"
		        ),
		    "view readiness ABI"
		);
		DWORD frameCloak = 0, coreCloak = 0;
		check(
		    DwmGetWindowAttribute(frame, DWMWA_CLOAKED, &frameCloak, sizeof(frameCloak)),
		    "Read Settings frame cloak"
		);
		check(
		    DwmGetWindowAttribute(core, DWMWA_CLOAKED, &coreCloak, sizeof(coreCloak)),
		    "Read Settings content cloak"
		);
		// Restoring a previously presented frame can leave the same CoreWindow
		// detached while the wrapper's readiness flags remain complete. Repair
		// only the pair whose native attachment we already observed as stable.
		if (completedFrame == frame && completedCore == core && completedPid == pid
		    && GetParent(core) == nullptr && !frameCloak && !coreCloak && IsWindowVisible(frame)
		    && IsWindowVisible(core))
		{
			const auto style = GetWindowLongPtrW(core, GWL_STYLE);
			SetLastError(ERROR_SUCCESS);
			if (!SetWindowLongPtrW(core, GWL_STYLE, (style & ~WS_POPUP) | WS_CHILD) && GetLastError())
				throw std::runtime_error("Restore Settings content style failed");
			SetLastError(ERROR_SUCCESS);
			if (!SetParent(core, frame) && GetLastError()) {
				SetWindowLongPtrW(core, GWL_STYLE, style);
				throw std::runtime_error("Restore Settings content attachment failed");
			}
			RECT client {};
			require(GetClientRect(frame, &client), "Read restored Settings client size");
			require(
			    SetWindowPos(
			        core,
			        nullptr,
			        contentInsets.left,
			        contentInsets.top,
			        client.right - contentInsets.left - contentInsets.right,
			        client.bottom - contentInsets.top - contentInsets.bottom,
			        SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED
			    ),
			    "Size restored Settings content"
			);
			repairedFrame = frame;
			repairedCore = core;
			repairedPid = pid;
			report(false, "Restored Settings content attachment.");
		}
		if (repairedFrame == frame && repairedCore == core && repairedPid == pid
		    && GetParent(core) == frame)
		{
			RECT frameClient {}, coreRectangle {};
			require(
			    GetClientRect(frame, &frameClient) && GetWindowRect(core, &coreRectangle),
			    "Read restored Settings client sizes"
			);
			MapWindowPoints(nullptr, frame, reinterpret_cast<POINT*>(&coreRectangle), 2);
			const RECT target {
			    contentInsets.left,
			    contentInsets.top,
			    frameClient.right - contentInsets.right,
			    frameClient.bottom - contentInsets.bottom
			};
			if (!EqualRect(&coreRectangle, &target))
				require(
				    SetWindowPos(
				        core,
				        nullptr,
				        target.left,
				        target.top,
				        target.right - target.left,
				        target.bottom - target.top,
				        SWP_NOZORDER | SWP_NOACTIVATE
				    ),
				    "Resize restored Settings content"
				);
		}
		if (GetParent(core) == frame && !frameCloak && !coreCloak && IsWindowVisible(frame)
		    && IsWindowVisible(core))
		{
			const auto now = GetTickCount64();
			if (confirmingFrame != frame || confirmingCore != core || confirmingPid != pid) {
				confirmingFrame = frame;
				confirmingCore = core;
				confirmingPid = pid;
				confirmedSince = now;
			}
			if (now - confirmedSince < 1000) return;
			const bool firstPresentation =
			    completedFrame != frame || completedCore != core || completedPid != pid;
			if (firstPresentation) {
				// Preserve the native frame's border insets when recovering this pair.
				RECT frameClient {}, content {};
				require(
				    GetClientRect(frame, &frameClient) && GetWindowRect(core, &content),
				    "Read native Settings content insets"
				);
				MapWindowPoints(nullptr, frame, reinterpret_cast<POINT*>(&content), 2);
				contentInsets = {
				    content.left,
				    content.top,
				    frameClient.right - content.right,
				    frameClient.bottom - content.bottom
				};
			}
			if (firstPresentation || (activationRequested && centeredGeneration != generation)) {
				POINT pointer {};
				const auto monitor = GetCursorPos(&pointer)
				                       ? MonitorFromPoint(pointer, MONITOR_DEFAULTTONEAREST)
				                       : MonitorFromWindow(frame, MONITOR_DEFAULTTONEAREST);
				if (qs::windows::settings::centerFrame(frame, monitor)) centeredGeneration = generation;
				else report(true, "Could not center Settings: %lu", GetLastError());
			}
			presentFrame(frame, core, firstPresentation || activationRequested);
			GUITHREADINFO input {};
			input.cbSize = sizeof(input);
			if (GetAncestor(GetForegroundWindow(), GA_ROOT) == frame
			    && GetGUIThreadInfo(GetWindowThreadProcessId(core, nullptr), &input)
			    && (input.hwndFocus == core || IsChild(core, input.hwndFocus)))
				activatedGeneration = generation;
			completedFrame = frame;
			completedCore = core;
			completedPid = pid;
			if (firstPresentation)
				report(false, "Settings frame and content are attached, visible and uncloaked.");
			return;
		}
		confirmedSince = 0;
		confirmingFrame = nullptr;
		confirmingCore = nullptr;
		report(
		    false,
		    "Settings presentation: frame=%p core=%p parent=%p cloak=%lu/%lu\n",
		    frame,
		    core,
		    GetParent(core),
		    frameCloak,
		    coreCloak
		);
		reinterpret_cast<void(STDMETHODCALLTYPE*)(IUnknown*, HWND)>(methods[24])(view.value, core);
		reinterpret_cast<void(STDMETHODCALLTYPE*)(IUnknown*, UINT, UINT)>(methods[40])(
		    view.value,
		    1,
		    1
		);
		reinterpret_cast<void(STDMETHODCALLTYPE*)(IUnknown*, UINT64)>(methods[41])(view.value, 0x26);
		const auto activationAddress =
		    symbols.address(module, "?HandleActivationRequested@UwpWindowWrapperBase@@UEAAXXZ");
		ComOwner activation;
		activation.value = firstMethodInterface(view.value, activationAddress, 42);
		require(activation.value != nullptr, "Settings activation interface");
		reinterpret_cast<void(STDMETHODCALLTYPE*)(IUnknown*)>(activationAddress)(activation.value);
		const auto uncloakAddress = symbols.address(
		    module,
		    "?SetShellCloak@UwpWindowWrapperBase@@UEAAJW4__MIDL___MIDL_itf_privilegedoperations_0000_"
		    "0002_0001@@@Z"
		);
		ComOwner visibility;
		visibility.value = firstMethodInterface(view.value, uncloakAddress);
		if (visibility.value)
			check(
			    reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IUnknown*, UINT)>(uncloakAddress)(
			        visibility.value,
			        0
			    ),
			    "Uncloak Settings view"
			);
		uncloakApplication(frame, activationRequested);
	}

private:
	void uncloakApplication(HWND frame, bool activate) {
		uintptr_t table = 0;
		std::array<uintptr_t, 7> methods {};
		require(
		    readPointers(applications.value, &table, 1)
		        && readPointers(reinterpret_cast<void*>(table), methods.data(), methods.size()),
		    "Application collection methods"
		);
		require(
		    methods[6]
		        == symbols.address(
		            module,
		            "?GetViewForHwnd@CApplicationViewManager@@UEAAJPEAUHWND__@@PEAPEAUIApplicationView@"
		            "@@Z"
		        ),
		    "Application lookup ABI"
		);
		ComOwner application;
		check(
		    reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IUnknown*, HWND, IUnknown**)>(methods[6])(
		        applications.value,
		        frame,
		        &application.value
		    ),
		    "Find Settings application view"
		);
		require(application.value != nullptr, "Settings application view");
		std::array<uintptr_t, 13> appMethods {};
		require(
		    readPointers(application.value, &table, 1)
		        && readPointers(reinterpret_cast<void*>(table), appMethods.data(), appMethods.size()),
		    "Application view methods"
		);
		require(
		    appMethods[12]
		        == symbols.address(
		            module,
		            "?SetCloak@CApplicationViewBase@@UEAAJW4APPLICATION_VIEW_CLOAK_TYPE@@H@Z"
		        ),
		    "Application cloak ABI"
		);
		// Uncloak the application frame as well as its CoreWindow content.
		check(
		    reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IUnknown*, UINT, BOOL)>(appMethods[12])(
		        application.value,
		        1,
		        FALSE
		    ),
		    "Uncloak Settings application"
		);
		if (activate) {
			require(
			    appMethods[7] == symbols.address(module, "?SwitchTo@CWinRTApplicationView@@UEAAJXZ"),
			    "Application switch ABI"
			);
			check(
			    reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IUnknown*)>(appMethods[7])(
			        application.value
			    ),
			    "Activate Settings application"
			);
		}
	}

	void presentFrame(HWND frame, HWND core, bool activate) {
		if (!largeIcon && !smallIcon) {
			wchar_t directory[MAX_PATH] {};
			const auto length = GetWindowsDirectoryW(directory, MAX_PATH);
			if (length && length < MAX_PATH) {
				const auto executable =
				    std::filesystem::path(directory) / L"ImmersiveControlPanel" / L"SystemSettings.exe";
				ExtractIconExW(executable.c_str(), 0, &largeIcon, &smallIcon, 1);
			}
			if (!largeIcon && !smallIcon) report(true, "Could not load the Settings gear icon.");
		}
		if (largeIcon
		    && reinterpret_cast<HICON>(SendMessageW(frame, WM_GETICON, ICON_BIG, 0)) != largeIcon)
			SendMessageW(frame, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(largeIcon));
		if (smallIcon
		    && reinterpret_cast<HICON>(SendMessageW(frame, WM_GETICON, ICON_SMALL, 0)) != smallIcon)
			SendMessageW(frame, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
		iconFrame = frame;
		const auto style = GetWindowLongPtrW(frame, GWL_STYLE);
		if ((style & WS_OVERLAPPEDWINDOW) != WS_OVERLAPPEDWINDOW) {
			SetLastError(ERROR_SUCCESS);
			if (!SetWindowLongPtrW(frame, GWL_STYLE, style | WS_OVERLAPPEDWINDOW) && GetLastError())
				report(true, "Could not restore Settings frame style: %lu", GetLastError());
			SetWindowPos(
			    frame,
			    nullptr,
			    0,
			    0,
			    0,
			    0,
			    SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED
			);
		}
		// A UWP frame can have caption styles while DWM non-client rendering is disabled.
		BOOL nonClientEnabled = FALSE;
		DwmGetWindowAttribute(
		    frame,
		    DWMWA_NCRENDERING_ENABLED,
		    &nonClientEnabled,
		    sizeof(nonClientEnabled)
		);
		if (!nonClientEnabled) {
			DWMNCRENDERINGPOLICY policy = DWMNCRP_ENABLED;
			const auto decorationResult =
			    DwmSetWindowAttribute(frame, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy));
			if (FAILED(decorationResult) && !decorationWarning) {
				report(true, "Could not enable Settings non-client rendering: 0x%08lx", decorationResult);
				decorationWarning = true;
			}
			SetWindowPos(
			    frame,
			    nullptr,
			    0,
			    0,
			    0,
			    0,
			    SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED
			);
		}
		if (!activate) return;
		// This STA does not own the application's input queue. Attach temporarily so
		// activation also directs keyboard input to the hosted content.
		const auto currentThread = GetCurrentThreadId();
		const auto frameThread = GetWindowThreadProcessId(frame, nullptr);
		const auto coreThread = GetWindowThreadProcessId(core, nullptr);
		const auto foregroundThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
		const bool attachedFrame =
		    frameThread != currentThread && AttachThreadInput(currentThread, frameThread, TRUE);
		const bool attachedCore = coreThread != currentThread && coreThread != frameThread
		                       && AttachThreadInput(currentThread, coreThread, TRUE);
		const bool attachedForeground = foregroundThread && foregroundThread != currentThread
		                             && foregroundThread != frameThread
		                             && foregroundThread != coreThread
		                             && AttachThreadInput(currentThread, foregroundThread, TRUE);
		BringWindowToTop(frame);
		if (SetForegroundWindow(frame)) {
			SetActiveWindow(frame);
			SetFocus(core);
		}
		if (attachedForeground) AttachThreadInput(currentThread, foregroundThread, FALSE);
		if (attachedCore) AttachThreadInput(currentThread, coreThread, FALSE);
		if (attachedFrame) AttachThreadInput(currentThread, frameThread, FALSE);
	}

	HICON largeIcon = nullptr;
	HICON smallIcon = nullptr;
	HWND iconFrame = nullptr;
	unsigned activatedGeneration = 0;
	unsigned centeredGeneration = 0;
	bool decorationWarning = false;
	Symbols& symbols;
	HMODULE module;
	ComOwner dispatcher;
	ComOwner applications;
	ComOwner collection;
	uintptr_t getViews = 0;
	uintptr_t destroyViews = 0;
	HWND completedFrame = nullptr;
	HWND completedCore = nullptr;
	DWORD completedPid = 0;
	HWND repairedFrame = nullptr;
	HWND repairedCore = nullptr;
	DWORD repairedPid = 0;
	RECT contentInsets {};
	HWND confirmingFrame = nullptr;
	HWND confirmingCore = nullptr;
	DWORD confirmingPid = 0;
	ULONGLONG confirmedSince = 0;
};

} // namespace

namespace qs::windows {

struct ImmersiveHost::Impl {
	std::filesystem::path cache;
	Logger logger;
	std::atomic<State> state {State::Starting};
	std::atomic<bool> stopping {false};
	std::atomic<DWORD> threadId {0};
	std::thread worker;

	Impl(std::filesystem::path cache, Logger logger)
	    : cache(std::move(cache))
	    , logger(std::move(logger)) {
		if (GetShellWindow()) {
			state = State::Inactive;
			return;
		}
		worker = std::thread([this] { run(); });
	}

	~Impl() {
		stopping = true;
		if (const auto id = threadId.load()) PostThreadMessageW(id, WM_QUIT, 0, 0);
		if (worker.joinable()) worker.join();
	}

	void run() {
		hostLogger = &logger;
		MSG message {};
		PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
		threadId = GetCurrentThreadId();
		try {
			require(
			    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != nullptr,
			    "Settings host DPI context"
			);
			Apartment apartment;
			Module twinui(L"twinui.pcshell.dll");
			Module provider(L"Windows.ImmersiveShell.ServiceProvider.dll");
			// Fail closed after a Windows update. Matching method names alone
			// cannot prove that a new private implementation has compatible behavior.
			require(
			    symbolKey(codeView(twinui.handle)) == "9507F3DAD47F5F026569FC50273C40D71"
			        && symbolKey(codeView(provider.handle)) == "CCFF947E577F802DDBB454842B7AA3A31",
			    "Windows immersive DLL build is not verified"
			);
			Symbols symbols(cache, &stopping);
			symbols.load(twinui.handle, cache);
			symbols.load(provider.handle, cache);
			if (stopping) return;
			ShellWindow shell;
			BandImport band(symbols, twinui.handle);
			ComOwner builder;
			check(
			    CoCreateInstance(
			        builderClass,
			        nullptr,
			        CLSCTX_INPROC_SERVER,
			        __uuidof(IUnknown),
			        reinterpret_cast<void**>(&builder.value)
			    ),
			    "Create shell builder"
			);
			const auto create = (*reinterpret_cast<uintptr_t**>(builder.value))[3];
			require(create == symbols.address(twinui.handle, createName), "builder method validation");
			ComOwner controller;
			check(
			    reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IUnknown*, IUnknown**)>(create)(
			        builder.value,
			        &controller.value
			    ),
			    "Create immersive controller"
			);
			require(controller.value != nullptr, "controller result");
			setComponentPolicy(symbols, twinui.handle, provider.handle, controller.value);
			const auto start = (*reinterpret_cast<uintptr_t**>(controller.value))[3];
			const auto stop = (*reinterpret_cast<uintptr_t**>(controller.value))[4];
			require(start == symbols.address(provider.handle, startName), "controller startup ABI");
			require(
			    stop == symbols.address(provider.handle, "?Stop@CImmersiveShellController@@UEAAJXZ"),
			    "controller shutdown ABI"
			);
			struct StopController {
				IUnknown* controller;
				uintptr_t method;
				~StopController() {
					const auto result =
					    reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IUnknown*)>(method)(controller);
					if (FAILED(result))
						report(
						    true,
						    "Immersive controller shutdown failed: 0x%08lx",
						    static_cast<unsigned long>(result)
						);
				}
			} stopController {controller.value, stop};
			check(
			    reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(IUnknown*)>(start)(controller.value),
			    "Start immersive controller"
			);
			SettingsPresentation presentation(symbols, twinui.handle);
			const auto timer = SetTimer(nullptr, 0, 250, nullptr);
			require(timer != 0, "Settings presentation timer");
			state = State::Ready;
			report(false, "Native Windows Settings host ready");
			std::string previousError;
			while (!stopping && GetMessageW(&message, nullptr, 0, 0) > 0) {
				if (message.message == WM_TIMER && message.wParam == timer) {
					try {
						presentation.tick();
						previousError.clear();
					} catch (const std::exception& error) {
						if (previousError != error.what()) {
							previousError = error.what();
							report(true, "Settings presentation pending: %s", error.what());
						}
					}
				}
				TranslateMessage(&message);
				DispatchMessageW(&message);
			}
			KillTimer(nullptr, timer);
		} catch (const std::exception& error) {
			state = State::Failed;
			report(
			    true,
			    "Windows Settings host unavailable: %s. Keep Explorer running for Settings.",
			    error.what()
			);
		}
		threadId = 0;
		hostLogger = nullptr;
	}
};

ImmersiveHost::ImmersiveHost(std::filesystem::path cache, Logger logger)
    : impl(std::make_unique<Impl>(std::move(cache), std::move(logger))) {}
ImmersiveHost::~ImmersiveHost() = default;
ImmersiveHost::State ImmersiveHost::state() const { return impl->state.load(); }
void ImmersiveHost::requestSettingsActivation() {
	settingsActivationDeadline = GetTickCount64() + 5000;
	++settingsActivationGeneration;
}

} // namespace qs::windows
