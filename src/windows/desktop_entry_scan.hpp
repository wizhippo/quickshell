#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace qs::windows {
struct ApplicationEntry {
	std::wstring id;
	std::wstring name;
};
struct ApplicationScan {
	std::vector<ApplicationEntry> entries;
	std::uint32_t error = 0;
};

// No Windows SDK or Qt headers cross this boundary. The COM scanner is built
// independently of quickshell-core's flags, includes and precompiled headers.
ApplicationScan scanApplications();
} // namespace qs::windows
