#pragma once
#include <string>
#include <Windows.h>

struct Config
{
	std::string moduleDir;       // Directory containing TimecycleEditor.asi
	std::string iniPath;
	std::string timecycleDir;    // Active timecycle XML directory
	std::string liveDir;         // Non-destructive live preview cache
	std::string exportDir;       // Exported XML directory
	std::string backupDir;       // Backup directory for Save-to-Original

	int         toggleKey       = VK_OEM_COMMA; // ',' key (0xBC)
	std::string toggleKeyLabel  = ",";
	bool        redirectFromFolder = true;

	float       stepDefault     = 0.05f;
	float       stepFine        = 0.005f;
	float       stepCoarse      = 0.50f;

	float       menuX           = 0.015f;
	float       menuY           = 0.035f;
	float       menuWidth       = 0.275f;
	int         maxVisibleRows  = 14;

	static Config& get();
	void load(HMODULE selfModule);
	void saveDefaultIniIfMissing() const;
	static int parseKeyCode(const std::string& raw, std::string& labelOut);
};
