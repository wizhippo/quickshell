#include "desktop_entry_scan.hpp"

#include <algorithm>
#include <memory>
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>

namespace qs::windows {
namespace {
struct Uninitialize {
	bool owned;
	~Uninitialize() { if (owned) CoUninitialize(); }
};
struct FreeComMemory {
	void operator()(void* value) const { CoTaskMemFree(value); }
};
template <typename T> struct ComPtr {
	T* value = nullptr;
	~ComPtr() { if (value) value->Release(); }
};
std::wstring displayName(IShellItem* item, SIGDN format) {
	LPWSTR raw = nullptr;
	const auto result = item->GetDisplayName(format, &raw);
	std::unique_ptr<wchar_t, FreeComMemory> memory(raw);
	return SUCCEEDED(result) && raw ? std::wstring(raw) : std::wstring();
}
} // namespace

ApplicationScan scanApplications() {
	ApplicationScan result;
	const auto apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE) {
		result.error = static_cast<std::uint32_t>(apartment);
		return result;
	}
	Uninitialize uninitialize {SUCCEEDED(apartment)};
	PIDLIST_ABSOLUTE raw = nullptr;
	auto status = SHParseDisplayName(L"shell:AppsFolder", nullptr, &raw, 0, nullptr);
	std::unique_ptr<ITEMIDLIST, FreeComMemory> pidl(raw);
	if (FAILED(status)) {
		result.error = static_cast<std::uint32_t>(status);
		return result;
	}
	ComPtr<IShellItem> folder;
	status = SHCreateItemFromIDList(pidl.get(), __uuidof(IShellItem), reinterpret_cast<void**>(&folder.value));
	if (FAILED(status)) {
		result.error = static_cast<std::uint32_t>(status);
		return result;
	}
	const auto folderPrefix = displayName(folder.value, SIGDN_DESKTOPABSOLUTEPARSING) + L'\\';
	ComPtr<IEnumShellItems> enumerator;
	status = folder.value->BindToHandler(nullptr, BHID_EnumItems, __uuidof(IEnumShellItems), reinterpret_cast<void**>(&enumerator.value));
	if (FAILED(status)) {
		result.error = static_cast<std::uint32_t>(status);
		return result;
	}
	for (;;) {
		ComPtr<IShellItem> item;
		status = enumerator.value->Next(1, &item.value, nullptr);
		if (status == S_FALSE) break;
		if (FAILED(status)) {
			result.entries.clear();
			result.error = static_cast<std::uint32_t>(status);
			return result;
		}
		if (!item.value) continue;
		auto name = displayName(item.value, SIGDN_NORMALDISPLAY);
		auto id = displayName(item.value, SIGDN_DESKTOPABSOLUTEPARSING);
		// AppsFolder's absolute parsing name prefixes the AUMID with the
		// folder namespace. Keep the child identifier for Explorer activation.
		// A desktop AUMID may itself contain backslashes (known-folder GUID
		// plus executable path). Never truncate those child identifiers.
		if (id.starts_with(folderPrefix)) id.erase(0, folderPrefix.length());
		if (!name.empty() && !id.empty()) result.entries.push_back({std::move(id), std::move(name)});
	}
	std::sort(result.entries.begin(), result.entries.end(), [](const auto& a, const auto& b) {
		return a.id < b.id;
	});
	result.entries.erase(std::unique(result.entries.begin(), result.entries.end(), [](const auto& a, const auto& b) {
		return a.id == b.id;
	}), result.entries.end());
	return result;
}
} // namespace qs::windows
