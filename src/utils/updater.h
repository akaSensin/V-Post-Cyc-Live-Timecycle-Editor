#pragma once
#include <string>

namespace updater
{
	inline constexpr const char* kModVersion    = "1.0.0";
	inline constexpr const char* kGithubProfile = "https://github.com/akaSensin";
	inline constexpr const char* kReleasesUrl   = "https://github.com/akaSensin/V-Post-Cyc-Live-Timecycle-Editor/releases/latest";

	enum class Status
	{
		Idle = 0,
		Checking,
		UpToDate,
		UpdateAvailable,
		Error
	};

	void checkAsync();
	Status status();
	std::string statusLabel();
	void openReleasesPage();
	void openGithubPage();
}
