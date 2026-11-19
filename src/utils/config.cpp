#include "config.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace
{
	std::string trim(std::string s)
	{
		auto isWs = [](unsigned char c) { return std::isspace(c) != 0; };
		while (!s.empty() && isWs((unsigned char)s.front())) s.erase(s.begin());
		while (!s.empty() && isWs((unsigned char)s.back()))  s.pop_back();
		return s;
	}

	std::string toUpper(std::string s)
	{
		for (auto& c : s) c = (char)std::toupper((unsigned char)c);
		return s;
	}

	bool parseBool(const std::string& v, bool def)
	{
		std::string u = toUpper(trim(v));
		if (u == "1" || u == "TRUE" || u == "YES" || u == "ON")  return true;
		if (u == "0" || u == "FALSE" || u == "NO" || u == "OFF") return false;
		return def;
	}

	float parseFloat(const std::string& v, float def)
	{
		if (v.empty()) return def;
		char* end = nullptr;
		float res = std::strtof(v.c_str(), &end);
		return (end != v.c_str()) ? res : def;
	}

	int parseInt(const std::string& v, int def)
	{
		if (v.empty()) return def;
		char* end = nullptr;
		long res = std::strtol(v.c_str(), &end, 0);
		return (end != v.c_str()) ? (int)res : def;
	}

	std::string resolveDir(const std::string& baseDir, const std::string& val)
	{
		fs::path p(val);
		if (p.is_absolute()) return p.lexically_normal().string();
		return (fs::path(baseDir) / p).lexically_normal().string();
	}
}

Config& Config::get()
{
	static Config inst;
	return inst;
}

int Config::parseKeyCode(const std::string& raw, std::string& labelOut)
{
	std::string t = trim(raw);
	if (t.empty())
	{
		labelOut = ",";
		return VK_OEM_COMMA;
	}

	if (t == "," || toUpper(t) == "COMMA" || toUpper(t) == "VK_OEM_COMMA")
	{
		labelOut = ",";
		return VK_OEM_COMMA;
	}
	if (t == "." || toUpper(t) == "PERIOD" || toUpper(t) == "VK_OEM_PERIOD")
	{
		labelOut = ".";
		return VK_OEM_PERIOD;
	}
	if (t == "/" || toUpper(t) == "SLASH" || toUpper(t) == "VK_OEM_2")
	{
		labelOut = "/";
		return VK_OEM_2;
	}
	if (t == ";" || toUpper(t) == "SEMICOLON" || toUpper(t) == "VK_OEM_1")
	{
		labelOut = ";";
		return VK_OEM_1;
	}
	if (t == "[" || toUpper(t) == "LBRACKET" || toUpper(t) == "VK_OEM_4")
	{
		labelOut = "[";
		return VK_OEM_4;
	}
	if (t == "]" || toUpper(t) == "RBRACKET" || toUpper(t) == "VK_OEM_6")
	{
		labelOut = "]";
		return VK_OEM_6;
	}
	if (t == "`" || t == "~" || toUpper(t) == "TILDE" || toUpper(t) == "VK_OEM_3")
	{
		labelOut = "`";
		return VK_OEM_3;
	}

	std::string u = toUpper(t);
	if (u.rfind("VK_", 0) == 0) u = u.substr(3);

	if (u == "INSERT" || u == "INS")   { labelOut = "INS"; return VK_INSERT; }
	if (u == "HOME")                   { labelOut = "HOME"; return VK_HOME; }
	if (u == "END")                    { labelOut = "END"; return VK_END; }
	if (u == "DELETE" || u == "DEL")   { labelOut = "DEL"; return VK_DELETE; }
	if (u == "PAGEUP" || u == "PGUP")  { labelOut = "PGUP"; return VK_PRIOR; }
	if (u == "PAGEDOWN" || u == "PGDN"){ labelOut = "PGDN"; return VK_NEXT; }
	if (u == "TAB")                    { labelOut = "TAB"; return VK_TAB; }
	if (u == "SPACE")                  { labelOut = "SPACE"; return VK_SPACE; }
	if (u == "MULTIPLY" || u == "*")   { labelOut = "NUM*"; return VK_MULTIPLY; }
	if (u == "ADD" || u == "+")        { labelOut = "NUM+"; return VK_ADD; }
	if (u == "SUBTRACT" || u == "-")   { labelOut = "NUM-"; return VK_SUBTRACT; }

	if (u.size() >= 2 && u[0] == 'F')
	{
		int fn = std::atoi(u.c_str() + 1);
		if (fn >= 1 && fn <= 24)
		{
			labelOut = u;
			return VK_F1 + (fn - 1);
		}
	}

	if (u.rfind("NUMPAD", 0) == 0 && u.size() == 7 && std::isdigit((unsigned char)u[6]))
	{
		labelOut = u;
		return VK_NUMPAD0 + (u[6] - '0');
	}

	if (u.size() == 1 && std::isalnum((unsigned char)u[0]))
	{
		labelOut = u;
		return (int)(unsigned char)u[0];
	}

	char* end = nullptr;
	long num = std::strtol(t.c_str(), &end, 0);
	if (end != t.c_str() && num > 0 && num < 256)
	{
		labelOut = t;
		return (int)num;
	}

	labelOut = ",";
	return VK_OEM_COMMA;
}

void Config::saveDefaultIniIfMissing() const
{
	std::error_code ec;
	if (fs::exists(iniPath, ec)) return;

	std::ofstream out(iniPath, std::ios::binary);
	if (!out) return;

	out <<
		"; TimecycleEditor configuration\n"
		"; ToggleKey accepts: , (comma), ., /, ;, F1-F24, INSERT, HOME, END, A-Z, NUMPAD0-NUMPAD9, or hex VK code (e.g. 0xBC)\n"
		"[General]\n"
		"ToggleKey = ,\n"
		"RedirectFromFolder = true\n"
		"TimecycleFolder = timecycle\n"
		"ExportFolder = timecycle/export\n"
		"\n"
		"[Editor]\n"
		"StepDefault = 0.05\n"
		"StepFine = 0.005\n"
		"StepCoarse = 0.50\n"
		"\n"
		"[UI]\n"
		"MenuX = 0.015\n"
		"MenuY = 0.035\n"
		"MenuWidth = 0.275\n"
		"MaxVisibleRows = 14\n";
}

void Config::load(HMODULE selfModule)
{
	char modPath[MAX_PATH]{};
	GetModuleFileNameA(selfModule, modPath, MAX_PATH);
	fs::path mp(modPath);
	moduleDir = mp.parent_path().string();
	iniPath   = (mp.parent_path() / "TimecycleEditor.ini").string();

	saveDefaultIniIfMissing();

	std::string tcFolderRel     = "timecycle";
	std::string exportFolderRel = "timecycle/export";

	std::ifstream in(iniPath);
	if (in)
	{
		std::string line;
		while (std::getline(in, line))
		{
			// Strip inline comments only if preceded by whitespace (so ';' key works if bare)
			size_t hashPos = line.find('#');
			if (hashPos != std::string::npos) line.resize(hashPos);
			std::string t = trim(line);
			if (t.empty() || t[0] == ';' || t[0] == '[') continue;

			size_t eq = t.find('=');
			if (eq == std::string::npos) continue;

			std::string key = toUpper(trim(t.substr(0, eq)));
			std::string val = trim(t.substr(eq + 1));

			if      (key == "TOGGLEKEY")          toggleKey = parseKeyCode(val, toggleKeyLabel);
			else if (key == "REDIRECTFROMFOLDER") redirectFromFolder = parseBool(val, redirectFromFolder);
			else if (key == "TIMECYCLEFOLDER")    { if (!val.empty()) tcFolderRel = val; }
			else if (key == "EXPORTFOLDER")       { if (!val.empty()) exportFolderRel = val; }
			else if (key == "STEPDEFAULT")        stepDefault = std::max(0.0001f, parseFloat(val, stepDefault));
			else if (key == "STEPFINE")           stepFine    = std::max(0.0001f, parseFloat(val, stepFine));
			else if (key == "STEPCOARSE")         stepCoarse  = std::max(0.001f,  parseFloat(val, stepCoarse));
			else if (key == "MENUX")              menuX       = std::clamp(parseFloat(val, menuX), 0.0f, 0.8f);
			else if (key == "MENUY")              menuY       = std::clamp(parseFloat(val, menuY), 0.0f, 0.8f);
			else if (key == "MENUWIDTH")          menuWidth   = std::clamp(parseFloat(val, menuWidth), 0.18f, 0.6f);
			else if (key == "MAXVISIBLEROWS")     maxVisibleRows = std::clamp(parseInt(val, maxVisibleRows), 6, 24);
		}
	}

	timecycleDir = resolveDir(moduleDir, tcFolderRel);
	exportDir    = resolveDir(moduleDir, exportFolderRel);
	liveDir      = (fs::path(timecycleDir) / "_live").string();
	backupDir    = (fs::path(timecycleDir) / "backup").string();
}
