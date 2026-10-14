#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace qs::windows {

// Native implementation deliberately has no Qt or Windows SDK types in its
// interface. One instance belongs to the launcher, independently of QML reload.
class ImmersiveHost {
public:
	enum class State { Inactive, Starting, Ready, Failed };
	using Logger = std::function<void(bool warning, const std::string& message)>;

	ImmersiveHost(std::filesystem::path symbolCache, Logger logger);
	~ImmersiveHost();
	ImmersiveHost(const ImmersiveHost&) = delete;
	ImmersiveHost& operator=(const ImmersiveHost&) = delete;
	[[nodiscard]] State state() const;
	// Called after a successful ms-settings activation, including reuse of a frame.
	static void requestSettingsActivation();

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};

} // namespace qs::windows
