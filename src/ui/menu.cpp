#include "menu.h"
#include "../core/categories.h"
#include "../core/tc_engine.h"
#include "../utils/config.h"
#include "../utils/log.h"
#include "../utils/updater.h"

#include <natives.h>
#include <types.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
	enum class Page
	{
		Main = 0,
		CycleFileList,
		CycleRegionList,
		CycleCategoryList,
		CycleVarList,
		ModFileList,
		ModGroupList,
		ModVarList,
		WorldControls,
		EditorSettings,
		ThemeSettings,
		SaveExport
	};

	struct ThemePreset
	{
		const char* name;
		int r, g, b;
	};

	constexpr ThemePreset kThemePresets[] = {
		{ "Ice Blue", 56,  189, 248 },
		{ "Emerald",  52,  211, 153 },
		{ "Amber",    251, 191, 36  },
		{ "Crimson",  248, 113, 113 },
		{ "Cyan",     34,  211, 238 },
		{ "Orange",   251, 146, 60  },
		{ "Silver",   203, 213, 225 }
	};
	constexpr int kNumThemePresets = (int)(sizeof(kThemePresets) / sizeof(kThemePresets[0]));

	struct MenuState
	{
		bool open = false;
		std::vector<Page> stack;
		std::vector<int>  cursorStack;
		int  cursor = 0;

		// Selection context
		int  cycleDocIdx   = 0;
		int  cycleGroupIdx = 0;      // 0 = GLOBAL, 1 = URBAN
		VarCategory category = VarCategory::All;
		int  keyframeIdx   = 6;      // 0..12
		bool editAllHours  = false;
		bool syncTimeToSlot = true;  // Auto-sync & lock game clock when scrubbing keyframes

		int  modDocIdx     = 0;
		int  modGroupIdx   = 0;
		bool previewMod    = false;
		float previewStrength = 1.0f;

		// Editing state
		float stepSize     = 0.05f;
		bool  clockFrozen  = false;
		int   frozenHour   = 12;
		int   frozenMinute = 0;
		bool  weatherLocked = false;
		std::string lockedWeather;

		// UI Theme state
		int themePresetIdx = 0;

		// On-screen keyboard prompt
		bool  kbActive     = false;
		bool  kbIsMod      = false;
		size_t kbRowIdx    = 0;
		int   kbModValSlot = 0; // 0 = v1, 1 = v2

		// Status toast
		std::string statusText;
		ULONGLONG   statusUntil = 0;

		// Debounced cycle reload
		bool      cycleReloadPending = false;
		ULONGLONG cycleReloadAt      = 0;
	} g_st;

	// Key repeat tracking
	struct KeyState
	{
		bool      down       = false;
		ULONGLONG pressedAt  = 0;
		ULONGLONG lastRepeat = 0;
		bool      justPressed = false;
	};
	KeyState g_keys[256]{};

	void setStatus(const std::string& msg, DWORD ms = 2500)
	{
		g_st.statusText  = msg;
		g_st.statusUntil = GetTickCount64() + ms;
	}

	std::string currentWeatherLabel()
	{
		Hash h = GAMEPLAY::_GET_CURRENT_WEATHER_TYPE();
		const char* wn = tc_engine::weatherNameForHash(h);
		if (wn) return wn;
		char buf[24];
		std::snprintf(buf, sizeof(buf), "0x%08X", (unsigned int)h);
		return buf;
	}

	void applyLockedTime(int hour, int minute = 0)
	{
		g_st.frozenHour   = ((hour % 24) + 24) % 24;
		g_st.frozenMinute = std::clamp(minute, 0, 59);
		TIME::SET_CLOCK_TIME(g_st.frozenHour, g_st.frozenMinute, 0);
		if (g_st.clockFrozen)
			TIME::PAUSE_CLOCK(TRUE);
	}

	void syncThemePresetIdx()
	{
		const auto& cfg = Config::get();
		for (int i = 0; i < kNumThemePresets; ++i)
		{
			if (kThemePresets[i].r == cfg.accentR &&
				kThemePresets[i].g == cfg.accentG &&
				kThemePresets[i].b == cfg.accentB)
			{
				g_st.themePresetIdx = i;
				return;
			}
		}
		g_st.themePresetIdx = kNumThemePresets; // Custom
	}

	Page currentPage()
	{
		return g_st.stack.empty() ? Page::Main : g_st.stack.back();
	}

	void pushPage(Page p)
	{
		g_st.stack.push_back(p);
		g_st.cursorStack.push_back(g_st.cursor);
		g_st.cursor = 0;
	}

	void popPage()
	{
		if (g_st.stack.empty())
		{
			g_st.open = false;
			return;
		}
		g_st.stack.pop_back();
		g_st.cursor = g_st.cursorStack.empty() ? 0 : g_st.cursorStack.back();
		if (!g_st.cursorStack.empty()) g_st.cursorStack.pop_back();
	}

	bool consumeKey(int vk, bool allowRepeat = false)
	{
		if (vk < 0 || vk >= 256) return false;
		auto& k = g_keys[vk];
		if (k.justPressed)
		{
			k.justPressed = false;
			return true;
		}
		if (allowRepeat && k.down)
		{
			ULONGLONG now = GetTickCount64();
			if (now - k.pressedAt > 340 && now - k.lastRepeat > 55)
			{
				k.lastRepeat = now;
				return true;
			}
		}
		return false;
	}

	void clearJustPressed(int exceptVk = -1)
	{
		for (int i = 0; i < 256; ++i)
		{
			if (i != exceptVk)
				g_keys[i].justPressed = false;
		}
	}

	float effectiveStep()
	{
		const auto& cfg = Config::get();
		const float base = cfg.stepDefault > 0.0f ? cfg.stepDefault : 0.05f;
		const float scale = g_st.stepSize / base;
		bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
		bool ctrl  = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
		if (shift) return cfg.stepCoarse * scale;
		if (ctrl)  return cfg.stepFine * scale;
		return g_st.stepSize;
	}

	// --- Drawing helpers (normalized 0..1 screen coords) ---------------------
	void drawRectTL(float x, float y, float w, float h, int r, int g, int b, int a)
	{
		GRAPHICS::DRAW_RECT(x + w * 0.5f, y + h * 0.5f, w, h, r, g, b, a);
	}

	void drawText(const std::string& text, float x, float y, float scale, int r, int g, int b, int a, int font = 0, bool rightAlign = false, float wrapRight = 0.0f)
	{
		UI::SET_TEXT_FONT(font);
		UI::SET_TEXT_SCALE(0.0f, scale);
		UI::SET_TEXT_COLOUR(r, g, b, a);
		UI::SET_TEXT_DROPSHADOW(0, 0, 0, 0, 0);
		UI::SET_TEXT_EDGE(0, 0, 0, 0, 0);
		if (rightAlign)
		{
			UI::SET_TEXT_RIGHT_JUSTIFY(TRUE);
			UI::SET_TEXT_WRAP(0.0f, wrapRight);
		}
		else
		{
			UI::SET_TEXT_CENTRE(FALSE);
		}
		UI::_SET_TEXT_ENTRY((char*)"STRING");
		UI::_ADD_TEXT_COMPONENT_STRING((char*)text.c_str());
		UI::_DRAW_TEXT(x, y);
	}

	struct RowItem
	{
		std::string left;
		std::string right;
		bool        accent = false;
		bool        hasColor = false;
		int         cr = 0, cg = 0, cb = 0;
		size_t      dataIdx = 0;
		int         extra   = 0;
	};

	std::vector<size_t> filteredCycleRows()
	{
		std::vector<size_t> out;
		auto& docs = tc_engine::cycleDocs();
		if (g_st.cycleDocIdx < 0 || g_st.cycleDocIdx >= (int)docs.size()) return out;
		const auto& doc = docs[g_st.cycleDocIdx];
		if (g_st.cycleGroupIdx < 0 || g_st.cycleGroupIdx >= (int)doc.groups.size()) return out;

		for (size_t rIdx : doc.groups[g_st.cycleGroupIdx].rowIndices)
		{
			if (g_st.category == VarCategory::All ||
				tc_cat::classify(doc.rows[rIdx].name) == g_st.category)
			{
				out.push_back(rIdx);
			}
		}
		return out;
	}

	void scheduleCycleApply(int docIdx)
	{
		auto& docs = tc_engine::cycleDocs();
		if (docIdx < 0 || docIdx >= (int)docs.size()) return;
		docs[docIdx].writeLiveCache(Config::get().liveDir);
		g_st.cycleReloadPending = true;
		g_st.cycleReloadAt      = GetTickCount64() + 50;
	}

	void startKeyboardFloat(bool isMod, size_t rowIdx, int modValSlot, float currentVal)
	{
		char def[64];
		std::snprintf(def, sizeof(def), "%.4f", currentVal);
		g_st.kbActive     = true;
		g_st.kbIsMod      = isMod;
		g_st.kbRowIdx     = rowIdx;
		g_st.kbModValSlot = modValSlot;
		GAMEPLAY::DISPLAY_ONSCREEN_KEYBOARD(TRUE, (char*)"FMMC_KEY_TIP8", (char*)"", def, (char*)"", (char*)"", (char*)"", 24);
	}

	void pollKeyboardInput()
	{
		if (!g_st.kbActive) return;
		int st = GAMEPLAY::UPDATE_ONSCREEN_KEYBOARD();
		if (st == 0) return; // still editing
		g_st.kbActive = false;
		if (st != 1) return; // cancelled

		const char* res = GAMEPLAY::GET_ONSCREEN_KEYBOARD_RESULT();
		if (!res || !res[0]) return;
		char* end = nullptr;
		float val = std::strtof(res, &end);
		if (end == res) return;

		if (!g_st.kbIsMod)
		{
			auto& docs = tc_engine::cycleDocs();
			if (g_st.cycleDocIdx >= 0 && g_st.cycleDocIdx < (int)docs.size())
			{
				auto& doc = docs[g_st.cycleDocIdx];
				if (g_st.kbRowIdx < doc.rows.size())
				{
					auto& row = doc.rows[g_st.kbRowIdx];
					if (g_st.editAllHours)
					{
						for (float& v : row.values) v = val;
					}
					else if (g_st.keyframeIdx >= 0 && g_st.keyframeIdx < (int)row.values.size())
					{
						row.values[g_st.keyframeIdx] = val;
					}
					doc.markRowDirty(g_st.kbRowIdx);
					scheduleCycleApply(g_st.cycleDocIdx);
				}
			}
		}
		else
		{
			auto& docs = tc_engine::modDocs();
			if (g_st.modDocIdx >= 0 && g_st.modDocIdx < (int)docs.size())
			{
				auto& doc = docs[g_st.modDocIdx];
				if (g_st.kbRowIdx < doc.rows.size())
				{
					auto& row = doc.rows[g_st.kbRowIdx];
					if (g_st.kbModValSlot >= 0 && g_st.kbModValSlot < (int)row.values.size())
					{
						row.values[g_st.kbModValSlot] = val;
						doc.markRowDirty(g_st.kbRowIdx);
						float v1 = row.values.size() > 0 ? row.values[0] : 0.0f;
						float v2 = row.values.size() > 1 ? row.values[1] : 0.0f;
						tc_engine::applyModifierLive(row.group, row.name, v1, v2);
					}
				}
			}
		}
	}

	void buildRows(std::string& title, std::string& subtitle, std::vector<RowItem>& items)
	{
		items.clear();
		const Page p = currentPage();

		if (p == Page::Main)
		{
			title = "TIMECYCLE EDITOR";
			Hash curWeather = GAMEPLAY::_GET_CURRENT_WEATHER_TYPE();
			int  curHour    = TIME::GET_CLOCK_HOURS();
			std::string wn  = currentWeatherLabel();
			char subBuf[128];
			std::snprintf(subBuf, sizeof(subBuf), "%s | %s %02d:00%s",
				tc_engine::isMarshalReady() ? "LIVE" : "XML ONLY",
				wn.c_str(), curHour, g_st.clockFrozen ? " [LOCKED]" : "");
			subtitle = subBuf;

			int curCycIdx = tc_engine::findCycleIndexByWeatherHash(curWeather);
			std::string curLabel = "Quick Edit: " + wn;
			if (curCycIdx >= 0)
			{
				char buf[96];
				std::snprintf(buf, sizeof(buf), "Quick Edit: %s (%02d:00)", wn.c_str(), curHour);
				curLabel = buf;
			}

			items.push_back({ curLabel, ">", true });
			items.push_back({ "Weather Cycles (w_*.xml)", std::to_string(tc_engine::cycleDocs().size()) + " files >" });
			items.push_back({ "Timecycle Modifiers (mods)", std::to_string(tc_engine::modDocs().size()) + " files >" });
			items.push_back({ "World Time & Weather", wn + " >" });
			items.push_back({ "Editor Settings & UI Colors", ">" });
			items.push_back({ "Save / Export / Reset", ">" });
		}
		else if (p == Page::CycleFileList)
		{
			title    = "WEATHER CYCLES";
			subtitle = "Current: " + currentWeatherLabel();
			const Hash curWeather = GAMEPLAY::_GET_CURRENT_WEATHER_TYPE();
			const int  curIdx     = tc_engine::findCycleIndexByWeatherHash(curWeather);
			const auto& docs = tc_engine::cycleDocs();
			for (size_t i = 0; i < docs.size(); ++i)
			{
				const bool isCur = ((int)i == curIdx);
				std::string right;
				if (isCur) right += "[CURRENT] ";
				right += docs[i].dirty ? "*MOD >" : ">";
				items.push_back({ docs[i].filename, right, isCur || docs[i].dirty, false, 0, 0, 0, i });
			}
		}
		else if (p == Page::CycleRegionList)
		{
			const auto& docs = tc_engine::cycleDocs();
			if (g_st.cycleDocIdx < 0 || g_st.cycleDocIdx >= (int)docs.size()) return;
			const auto& doc = docs[g_st.cycleDocIdx];
			title    = doc.filename;
			subtitle = "Select Region";
			for (size_t i = 0; i < doc.groups.size(); ++i)
			{
				items.push_back({ doc.groups[i].name, std::to_string(doc.groups[i].rowIndices.size()) + " vars >", false, false, 0, 0, 0, i });
			}
		}
		else if (p == Page::CycleCategoryList)
		{
			const auto& docs = tc_engine::cycleDocs();
			if (g_st.cycleDocIdx < 0 || g_st.cycleDocIdx >= (int)docs.size()) return;
			const auto& doc = docs[g_st.cycleDocIdx];
			if (g_st.cycleGroupIdx < 0 || g_st.cycleGroupIdx >= (int)doc.groups.size()) return;
			title    = doc.filename;
			subtitle = doc.groups[g_st.cycleGroupIdx].name + " / Category";
			for (int c = 0; c < (int)VarCategory::Count; ++c)
			{
				items.push_back({ categoryName((VarCategory)c), ">", c == (int)VarCategory::Fog || c == (int)VarCategory::MetalsAndReflections, false, 0, 0, 0, (size_t)c });
			}
		}
		else if (p == Page::CycleVarList)
		{
			const auto& docs = tc_engine::cycleDocs();
			if (g_st.cycleDocIdx < 0 || g_st.cycleDocIdx >= (int)docs.size()) return;
			const auto& doc = docs[g_st.cycleDocIdx];
			if (g_st.cycleGroupIdx < 0 || g_st.cycleGroupIdx >= (int)doc.groups.size()) return;
			title    = categoryName(g_st.category);
			subtitle = doc.filename + " [" + doc.groups[g_st.cycleGroupIdx].name + "]";

			const int kf = std::clamp(g_st.keyframeIdx, 0, 12);
			std::string kfLabel = g_st.editAllHours ? "ALL 13 HOURS" : tc_cat::kKeyframeLabels[kf];
			items.push_back({ "< Keyframe Slot >", kfLabel, true, false, 0, 0, 0, 0, -1 });
			items.push_back({ "Lock Game Time to Slot", g_st.syncTimeToSlot ? "ON" : "OFF", g_st.syncTimeToSlot, false, 0, 0, 0, 0, -2 });

			auto rowIdxs = filteredCycleRows();
			for (size_t rIdx : rowIdxs)
			{
				const auto& r = doc.rows[rIdx];
				float v = (g_st.keyframeIdx >= 0 && g_st.keyframeIdx < (int)r.values.size()) ? r.values[g_st.keyframeIdx] : 0.0f;
				char valBuf[64];
				std::snprintf(valBuf, sizeof(valBuf), "%s%.4f", r.dirty ? "* " : "", v);

				RowItem item;
				item.left    = r.name;
				item.right   = valBuf;
				item.accent  = r.dirty;
				item.dataIdx = rIdx;
				item.extra   = 1;

				int ch = tc_cat::colorChannel(r.name);
				if (ch >= 1 && ch <= 3)
				{
					int cval = std::clamp((int)(v * 255.0f), 0, 255);
					item.hasColor = true;
					item.cr = (ch == 1) ? cval : 20;
					item.cg = (ch == 2) ? cval : 20;
					item.cb = (ch == 3) ? cval : 20;
				}
				items.push_back(std::move(item));
			}
		}
		else if (p == Page::ModFileList)
		{
			title    = "TIMECYCLE MODIFIERS";
			subtitle = "Select a timecycle_mods_*.xml file";
			const auto& docs = tc_engine::modDocs();
			for (size_t i = 0; i < docs.size(); ++i)
			{
				std::string right = std::to_string(docs[i].groups.size()) + " mods" + (docs[i].dirty ? " * >" : " >");
				items.push_back({ docs[i].filename, right, docs[i].dirty, false, 0, 0, 0, i });
			}
		}
		else if (p == Page::ModGroupList)
		{
			const auto& docs = tc_engine::modDocs();
			if (g_st.modDocIdx < 0 || g_st.modDocIdx >= (int)docs.size()) return;
			const auto& doc = docs[g_st.modDocIdx];
			title    = doc.filename;
			subtitle = "Select Modifier (" + std::to_string(doc.groups.size()) + ")";
			for (size_t i = 0; i < doc.groups.size(); ++i)
			{
				items.push_back({ doc.groups[i].name, std::to_string(doc.groups[i].rowIndices.size()) + " vars >", false, false, 0, 0, 0, i });
			}
		}
		else if (p == Page::ModVarList)
		{
			const auto& docs = tc_engine::modDocs();
			if (g_st.modDocIdx < 0 || g_st.modDocIdx >= (int)docs.size()) return;
			const auto& doc = docs[g_st.modDocIdx];
			if (g_st.modGroupIdx < 0 || g_st.modGroupIdx >= (int)doc.groups.size()) return;
			const auto& grp = doc.groups[g_st.modGroupIdx];
			title    = grp.name;
			subtitle = doc.filename + " (In-Place Live Patch)";

			items.push_back({ "Preview This Modifier", g_st.previewMod ? "ON" : "OFF", g_st.previewMod, false, 0, 0, 0, 0, -1 });
			char sBuf[32];
			std::snprintf(sBuf, sizeof(sBuf), "%.2f", g_st.previewStrength);
			items.push_back({ "< Preview Strength >", sBuf, false, false, 0, 0, 0, 0, -2 });

			for (size_t rIdx : grp.rowIndices)
			{
				const auto& r = doc.rows[rIdx];
				float v1 = r.values.size() > 0 ? r.values[0] : 0.0f;
				float v2 = r.values.size() > 1 ? r.values[1] : 0.0f;
				char b1[48], b2[48];
				std::snprintf(b1, sizeof(b1), "%s%.3f", r.dirty ? "* " : "", v1);
				std::snprintf(b2, sizeof(b2), "%s%.3f", r.dirty ? "* " : "", v2);
				items.push_back({ r.name + " [v1]", b1, r.dirty, false, 0, 0, 0, rIdx, 0 });
				items.push_back({ r.name + " [v2]", b2, r.dirty, false, 0, 0, 0, rIdx, 1 });
			}
		}
		else if (p == Page::WorldControls)
		{
			std::string curW = currentWeatherLabel();
			char hBuf[32];
			std::snprintf(hBuf, sizeof(hBuf), "%02d:%02d%s",
				TIME::GET_CLOCK_HOURS(), TIME::GET_CLOCK_MINUTES(),
				g_st.clockFrozen ? " [LOCK]" : "");
			title    = "WORLD TIME & WEATHER";
			subtitle = "Weather: " + curW + (g_st.weatherLocked ? " [LOCKED]" : "");

			items.push_back({ "< Set Clock Hour >", hBuf });
			items.push_back({ "Lock Time", g_st.clockFrozen ? "ON" : "OFF", g_st.clockFrozen });
			items.push_back({ "Lock Current Weather", g_st.weatherLocked ? "ON" : "OFF", g_st.weatherLocked });
			items.push_back({ "Clear Active Modifier", "APPLY" });

			static const char* weathers[] = {
				"CLEAR", "EXTRASUNNY", "CLOUDS", "OVERCAST", "RAIN",
				"THUNDER", "CLEARING", "NEUTRAL", "SMOG", "FOGGY", "XMAS", "HALLOWEEN"
			};
			for (const char* w : weathers)
			{
				const bool isCur = (_stricmp(curW.c_str(), w) == 0);
				items.push_back({ std::string("Weather: ") + w, isCur ? "CURRENT" : "SET", isCur });
			}
		}
		else if (p == Page::EditorSettings)
		{
			title    = "EDITOR SETTINGS";
			subtitle = "Hold Shift = 10x | Ctrl = 0.1x";
			char sBuf[32];
			std::snprintf(sBuf, sizeof(sBuf), "%.4f", g_st.stepSize);
			items.push_back({ "< Adjust Step Size >", sBuf });
			items.push_back({ "Edit All 13 Hours Together", g_st.editAllHours ? "ON" : "OFF", g_st.editAllHours });
			items.push_back({ "UI Theme & Colors", ">", true });
			items.push_back({ "Menu Key (from .ini)", Config::get().toggleKeyLabel });
			items.push_back({ "Check Update", updater::statusLabel(), updater::status() == updater::Status::UpdateAvailable });
			items.push_back({ "Mod Update (Releases)", "OPEN >" });
			items.push_back({ "GitHub Page (akaSensin)", "OPEN >" });
		}
		else if (p == Page::ThemeSettings)
		{
			const auto& cfg = Config::get();
			title    = "UI THEME & COLORS";
			subtitle = "Customize accent color & opacity";

			const char* presetLabel = (g_st.themePresetIdx >= 0 && g_st.themePresetIdx < kNumThemePresets)
				? kThemePresets[g_st.themePresetIdx].name
				: "Custom";
			items.push_back({ "< Color Preset >", presetLabel, true, true, cfg.accentR, cfg.accentG, cfg.accentB });
			items.push_back({ "< Accent Red >", std::to_string(cfg.accentR), false, true, cfg.accentR, 20, 20 });
			items.push_back({ "< Accent Green >", std::to_string(cfg.accentG), false, true, 20, cfg.accentG, 20 });
			items.push_back({ "< Accent Blue >", std::to_string(cfg.accentB), false, true, 20, 20, cfg.accentB });
			items.push_back({ "< Panel Opacity >", std::to_string(cfg.bgAlpha) });
			items.push_back({ "Save UI Colors to .ini", "SAVE" });
			items.push_back({ "Reset Default Colors", "RESET" });
		}
		else if (p == Page::SaveExport)
		{
			title    = "SAVE / EXPORT / RESET";
			subtitle = "Export never touches original XMLs";
			int dirtyCount = 0;
			for (const auto& d : tc_engine::cycleDocs()) if (d.dirty) ++dirtyCount;
			for (const auto& d : tc_engine::modDocs())   if (d.dirty) ++dirtyCount;

			items.push_back({ "Export All Modified to export/", std::to_string(dirtyCount) + " dirty", dirtyCount > 0 });
			items.push_back({ "Save All Modified to timecycle/", "with backup", dirtyCount > 0 });
			items.push_back({ "Reload Cycles In-Game", "Init(4)" });
			items.push_back({ "Discard All Live Changes", "RESET" });
		}
	}

	void handleInput(const std::vector<RowItem>& items)
	{
		if (consumeKey(VK_BACK) || consumeKey(VK_NUMPAD0))
		{
			AUDIO::PLAY_SOUND_FRONTEND(-1, (char*)"BACK", (char*)"HUD_FRONTEND_DEFAULT_SOUNDSET", FALSE);
			popPage();
			return;
		}

		const int n = (int)items.size();
		if (n > 0)
		{
			if (consumeKey(VK_UP, true) || consumeKey(VK_NUMPAD8, true))
			{
				g_st.cursor = (g_st.cursor - 1 + n) % n;
				AUDIO::PLAY_SOUND_FRONTEND(-1, (char*)"NAV_UP_DOWN", (char*)"HUD_FRONTEND_DEFAULT_SOUNDSET", FALSE);
			}
			if (consumeKey(VK_DOWN, true) || consumeKey(VK_NUMPAD2, true))
			{
				g_st.cursor = (g_st.cursor + 1) % n;
				AUDIO::PLAY_SOUND_FRONTEND(-1, (char*)"NAV_UP_DOWN", (char*)"HUD_FRONTEND_DEFAULT_SOUNDSET", FALSE);
			}
			if (g_st.cursor >= n) g_st.cursor = n - 1;
			if (g_st.cursor < 0)  g_st.cursor = 0;
		}

		const Page p = currentPage();
		const int leftPressed  = (consumeKey(VK_LEFT, true) || consumeKey(VK_NUMPAD4, true)) ? 1 : 0;
		const int rightPressed = (consumeKey(VK_RIGHT, true) || consumeKey(VK_NUMPAD6, true)) ? 1 : 0;
		const int dir = rightPressed - leftPressed;
		const bool enterPressed = consumeKey(VK_RETURN) || consumeKey(VK_NUMPAD5);
		const bool resetPressed = consumeKey('R') || consumeKey(VK_DELETE);

		if (n == 0) return;
		const auto& sel = items[g_st.cursor];

		// Page-specific actions
		if (p == Page::Main && enterPressed)
		{
			AUDIO::PLAY_SOUND_FRONTEND(-1, (char*)"SELECT", (char*)"HUD_FRONTEND_DEFAULT_SOUNDSET", FALSE);
			if (g_st.cursor == 0)
			{
				int idx = tc_engine::findCycleIndexByWeatherHash(GAMEPLAY::_GET_CURRENT_WEATHER_TYPE());
				if (idx < 0 && !tc_engine::cycleDocs().empty()) idx = 0;
				if (idx >= 0)
				{
					g_st.cycleDocIdx   = idx;
					g_st.cycleGroupIdx = 0;
					g_st.keyframeIdx   = tc_cat::hourToKeyframe(TIME::GET_CLOCK_HOURS());
					g_st.category      = VarCategory::Fog;
					pushPage(Page::CycleCategoryList);
				}
				else setStatus("No w_*.xml files found in timecycle/");
			}
			else if (g_st.cursor == 1) pushPage(Page::CycleFileList);
			else if (g_st.cursor == 2) pushPage(Page::ModFileList);
			else if (g_st.cursor == 3) pushPage(Page::WorldControls);
			else if (g_st.cursor == 4) pushPage(Page::EditorSettings);
			else if (g_st.cursor == 5) pushPage(Page::SaveExport);
		}
		else if (p == Page::CycleFileList && enterPressed)
		{
			g_st.cycleDocIdx = (int)sel.dataIdx;
			g_st.keyframeIdx = tc_cat::hourToKeyframe(TIME::GET_CLOCK_HOURS());
			pushPage(Page::CycleRegionList);
		}
		else if (p == Page::CycleRegionList && enterPressed)
		{
			g_st.cycleGroupIdx = (int)sel.dataIdx;
			pushPage(Page::CycleCategoryList);
		}
		else if (p == Page::CycleCategoryList && enterPressed)
		{
			g_st.category = (VarCategory)sel.dataIdx;
			pushPage(Page::CycleVarList);
		}
		else if (p == Page::CycleVarList)
		{
			if (sel.extra == -1)
			{
				if (dir != 0)
				{
					if (g_st.editAllHours)
					{
						g_st.editAllHours = false;
						g_st.keyframeIdx = (dir > 0) ? 0 : 12;
					}
					else
					{
						int next = g_st.keyframeIdx + dir;
						if (next < 0 || next > 12) g_st.editAllHours = true;
						else g_st.keyframeIdx = next;
					}
					if (!g_st.editAllHours && g_st.syncTimeToSlot && g_st.keyframeIdx >= 0 && g_st.keyframeIdx < 13)
					{
						g_st.clockFrozen = true;
						applyLockedTime(tc_cat::kKeyframeHours[g_st.keyframeIdx], 0);
					}
				}
				else if (enterPressed)
				{
					g_st.editAllHours = !g_st.editAllHours;
					if (!g_st.editAllHours && g_st.syncTimeToSlot && g_st.keyframeIdx >= 0 && g_st.keyframeIdx < 13)
					{
						g_st.clockFrozen = true;
						applyLockedTime(tc_cat::kKeyframeHours[g_st.keyframeIdx], 0);
					}
				}
			}
			else if (sel.extra == -2)
			{
				if (enterPressed || dir != 0)
				{
					g_st.syncTimeToSlot = !g_st.syncTimeToSlot;
					if (g_st.syncTimeToSlot && !g_st.editAllHours && g_st.keyframeIdx >= 0 && g_st.keyframeIdx < 13)
					{
						g_st.clockFrozen = true;
						applyLockedTime(tc_cat::kKeyframeHours[g_st.keyframeIdx], 0);
						setStatus("Time locked to keyframe slot");
					}
					else if (!g_st.syncTimeToSlot)
					{
						setStatus("Slot time sync OFF");
					}
				}
			}
			else if (sel.extra == 1)
			{
				auto& docs = tc_engine::cycleDocs();
				if (g_st.cycleDocIdx < 0 || g_st.cycleDocIdx >= (int)docs.size()) return;
				auto& doc = docs[g_st.cycleDocIdx];
				if (sel.dataIdx >= doc.rows.size()) return;
				auto& row = doc.rows[sel.dataIdx];
				if (dir != 0)
				{
					float delta = (float)dir * effectiveStep();
					if (g_st.editAllHours)
					{
						for (float& v : row.values) v += delta;
					}
					else if (g_st.keyframeIdx >= 0 && g_st.keyframeIdx < (int)row.values.size())
					{
						row.values[g_st.keyframeIdx] += delta;
					}
					doc.markRowDirty(sel.dataIdx);
					scheduleCycleApply(g_st.cycleDocIdx);
				}
				else if (enterPressed)
				{
					float curV = (g_st.keyframeIdx >= 0 && g_st.keyframeIdx < (int)row.values.size()) ? row.values[g_st.keyframeIdx] : 0.0f;
					startKeyboardFloat(false, sel.dataIdx, 0, curV);
				}
				else if (resetPressed)
				{
					if (g_st.editAllHours)
					{
						doc.resetRow(sel.dataIdx);
						setStatus("Reset " + row.name + " (all hours)");
					}
					else if (g_st.keyframeIdx >= 0 && g_st.keyframeIdx < (int)row.values.size() && g_st.keyframeIdx < (int)row.orig.size())
					{
						float origV = row.orig[g_st.keyframeIdx];
						row.values[g_st.keyframeIdx] = origV;
						bool stillDirty = false;
						for (size_t k = 0; k < row.values.size() && k < row.orig.size(); ++k)
						{
							if (row.values[k] != row.orig[k]) { stillDirty = true; break; }
						}
						if (stillDirty) doc.markRowDirty(sel.dataIdx);
						else            doc.resetRow(sel.dataIdx);
						char msg[96];
						std::snprintf(msg, sizeof(msg), "Reset %s -> %.4f", row.name.c_str(), origV);
						setStatus(msg);
					}
					AUDIO::PLAY_SOUND_FRONTEND(-1, (char*)"BACK", (char*)"HUD_FRONTEND_DEFAULT_SOUNDSET", FALSE);
					scheduleCycleApply(g_st.cycleDocIdx);
				}
			}
		}
		else if (p == Page::ModFileList && enterPressed)
		{
			g_st.modDocIdx = (int)sel.dataIdx;
			pushPage(Page::ModGroupList);
		}
		else if (p == Page::ModGroupList && enterPressed)
		{
			if (g_st.modGroupIdx != (int)sel.dataIdx && g_st.previewMod)
			{
				GRAPHICS::CLEAR_TIMECYCLE_MODIFIER();
				g_st.previewMod = false;
			}
			g_st.modGroupIdx = (int)sel.dataIdx;
			pushPage(Page::ModVarList);
		}
		else if (p == Page::ModVarList)
		{
			auto& docs = tc_engine::modDocs();
			if (g_st.modDocIdx < 0 || g_st.modDocIdx >= (int)docs.size()) return;
			auto& doc = docs[g_st.modDocIdx];
			if (g_st.modGroupIdx < 0 || g_st.modGroupIdx >= (int)doc.groups.size()) return;
			const auto& grp = doc.groups[g_st.modGroupIdx];

			if (sel.extra == -1 && (enterPressed || dir != 0))
			{
				g_st.previewMod = !g_st.previewMod;
				if (g_st.previewMod)
				{
					GRAPHICS::SET_TIMECYCLE_MODIFIER((char*)grp.name.c_str());
					GRAPHICS::SET_TIMECYCLE_MODIFIER_STRENGTH(g_st.previewStrength);
					setStatus("Previewing: " + grp.name);
				}
				else
				{
					GRAPHICS::CLEAR_TIMECYCLE_MODIFIER();
					setStatus("Modifier preview cleared");
				}
			}
			else if (sel.extra == -2 && dir != 0)
			{
				g_st.previewStrength = std::clamp(g_st.previewStrength + (float)dir * 0.05f, 0.0f, 2.0f);
				if (g_st.previewMod)
					GRAPHICS::SET_TIMECYCLE_MODIFIER_STRENGTH(g_st.previewStrength);
			}
			else if (sel.extra >= 0)
			{
				if (sel.dataIdx >= doc.rows.size()) return;
				auto& row = doc.rows[sel.dataIdx];
				if (dir != 0 && sel.extra < (int)row.values.size())
				{
					row.values[sel.extra] += (float)dir * effectiveStep();
					doc.markRowDirty(sel.dataIdx);
					float v1 = row.values.size() > 0 ? row.values[0] : 0.0f;
					float v2 = row.values.size() > 1 ? row.values[1] : 0.0f;
					tc_engine::applyModifierLive(row.group, row.name, v1, v2);
				}
				else if (enterPressed && sel.extra < (int)row.values.size())
				{
					startKeyboardFloat(true, sel.dataIdx, sel.extra, row.values[sel.extra]);
				}
				else if (resetPressed && sel.extra < (int)row.values.size() && sel.extra < (int)row.orig.size())
				{
					float origV = row.orig[sel.extra];
					row.values[sel.extra] = origV;
					bool stillDirty = false;
					for (size_t k = 0; k < row.values.size() && k < row.orig.size(); ++k)
					{
						if (row.values[k] != row.orig[k]) { stillDirty = true; break; }
					}
					if (stillDirty) doc.markRowDirty(sel.dataIdx);
					else            doc.resetRow(sel.dataIdx);
					float v1 = row.values.size() > 0 ? row.values[0] : 0.0f;
					float v2 = row.values.size() > 1 ? row.values[1] : 0.0f;
					tc_engine::applyModifierLive(row.group, row.name, v1, v2);
					AUDIO::PLAY_SOUND_FRONTEND(-1, (char*)"BACK", (char*)"HUD_FRONTEND_DEFAULT_SOUNDSET", FALSE);
					char msg[96];
					std::snprintf(msg, sizeof(msg), "Reset %s -> %.3f", row.name.c_str(), origV);
					setStatus(msg);
				}
			}
		}
		else if (p == Page::WorldControls)
		{
			if (g_st.cursor == 0 && dir != 0)
			{
				int h = (TIME::GET_CLOCK_HOURS() + dir + 24) % 24;
				applyLockedTime(h, 0);
			}
			else if (g_st.cursor == 1 && (enterPressed || dir != 0))
			{
				g_st.clockFrozen = !g_st.clockFrozen;
				if (g_st.clockFrozen)
				{
					applyLockedTime(TIME::GET_CLOCK_HOURS(), TIME::GET_CLOCK_MINUTES());
					setStatus("Time locked");
				}
				else
				{
					TIME::PAUSE_CLOCK(FALSE);
					setStatus("Time unlocked");
				}
			}
			else if (g_st.cursor == 2 && (enterPressed || dir != 0))
			{
				if (!g_st.weatherLocked)
				{
					const char* wn = tc_engine::weatherNameForHash(GAMEPLAY::_GET_CURRENT_WEATHER_TYPE());
					if (!wn)
					{
						setStatus("Unknown weather hash - pick one below");
					}
					else
					{
						g_st.weatherLocked = true;
						g_st.lockedWeather = wn;
						GAMEPLAY::CLEAR_WEATHER_TYPE_PERSIST();
						GAMEPLAY::SET_WEATHER_TYPE_NOW_PERSIST((char*)g_st.lockedWeather.c_str());
						setStatus("Weather locked: " + g_st.lockedWeather);
					}
				}
				else
				{
					g_st.weatherLocked = false;
					g_st.lockedWeather.clear();
					GAMEPLAY::CLEAR_WEATHER_TYPE_PERSIST();
					setStatus("Weather unlocked");
				}
			}
			else if (g_st.cursor == 3 && enterPressed)
			{
				GRAPHICS::CLEAR_TIMECYCLE_MODIFIER();
				g_st.previewMod = false;
				setStatus("Timecycle modifier cleared");
			}
			else if (g_st.cursor >= 4 && enterPressed)
			{
				static const char* weathers[] = {
					"CLEAR", "EXTRASUNNY", "CLOUDS", "OVERCAST", "RAIN",
					"THUNDER", "CLEARING", "NEUTRAL", "SMOG", "FOGGY", "XMAS", "HALLOWEEN"
				};
				int wi = g_st.cursor - 4;
				if (wi >= 0 && wi < 12)
				{
					GAMEPLAY::CLEAR_WEATHER_TYPE_PERSIST();
					GAMEPLAY::SET_WEATHER_TYPE_NOW_PERSIST((char*)weathers[wi]);
					if (g_st.weatherLocked)
						g_st.lockedWeather = weathers[wi];
					setStatus(std::string("Weather set: ") + weathers[wi]);
				}
			}
		}
		else if (p == Page::EditorSettings)
		{
			static const float steps[] = { 0.001f, 0.005f, 0.01f, 0.05f, 0.1f, 0.5f, 1.0f, 5.0f };
			if (g_st.cursor == 0 && dir != 0)
			{
				int idx = 0;
				float bestDiff = 1e9f;
				for (int i = 0; i < 8; ++i)
				{
					float d = std::abs(g_st.stepSize - steps[i]);
					if (d < bestDiff) { bestDiff = d; idx = i; }
				}
				idx = std::clamp(idx + dir, 0, 7);
				g_st.stepSize = steps[idx];
			}
			else if (g_st.cursor == 1 && (enterPressed || dir != 0))
			{
				g_st.editAllHours = !g_st.editAllHours;
			}
			else if (g_st.cursor == 2 && enterPressed)
			{
				syncThemePresetIdx();
				pushPage(Page::ThemeSettings);
			}
			else if (g_st.cursor == 4 && enterPressed)
			{
				updater::checkAsync();
				setStatus("Checking GitHub for updates...");
			}
			else if (g_st.cursor == 5 && enterPressed)
			{
				updater::openReleasesPage();
				setStatus("Opened latest releases page");
			}
			else if (g_st.cursor == 6 && enterPressed)
			{
				updater::openGithubPage();
				setStatus("Opened GitHub page");
			}
		}
		else if (p == Page::ThemeSettings)
		{
			auto& cfg = Config::get();
			const int colorStep = (GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 20
				: ((GetAsyncKeyState(VK_CONTROL) & 0x8000) ? 1 : 5);

			if (g_st.cursor == 0 && (dir != 0 || enterPressed))
			{
				int d = (dir != 0) ? dir : 1;
				int cur = (g_st.themePresetIdx >= 0 && g_st.themePresetIdx < kNumThemePresets)
					? g_st.themePresetIdx : -1;
				int next = (cur + d + kNumThemePresets) % kNumThemePresets;
				g_st.themePresetIdx = next;
				cfg.accentR = kThemePresets[next].r;
				cfg.accentG = kThemePresets[next].g;
				cfg.accentB = kThemePresets[next].b;
				setStatus(std::string("Theme: ") + kThemePresets[next].name);
			}
			else if (g_st.cursor == 1 && dir != 0)
			{
				cfg.accentR = std::clamp(cfg.accentR + dir * colorStep, 0, 255);
				syncThemePresetIdx();
			}
			else if (g_st.cursor == 2 && dir != 0)
			{
				cfg.accentG = std::clamp(cfg.accentG + dir * colorStep, 0, 255);
				syncThemePresetIdx();
			}
			else if (g_st.cursor == 3 && dir != 0)
			{
				cfg.accentB = std::clamp(cfg.accentB + dir * colorStep, 0, 255);
				syncThemePresetIdx();
			}
			else if (g_st.cursor == 4 && dir != 0)
			{
				cfg.bgAlpha = std::clamp(cfg.bgAlpha + dir * colorStep, 140, 255);
			}
			else if (g_st.cursor == 5 && enterPressed)
			{
				if (cfg.save())
					setStatus("Saved UI colors to TimecycleEditor.ini");
				else
					setStatus("Failed to save TimecycleEditor.ini");
			}
			else if (g_st.cursor == 6 && enterPressed)
			{
				cfg.accentR = 56;
				cfg.accentG = 189;
				cfg.accentB = 248;
				cfg.bgAlpha = 238;
				syncThemePresetIdx();
				cfg.save();
				setStatus("Reset UI colors to default");
			}
		}
		else if (p == Page::SaveExport && enterPressed)
		{
			if (g_st.cursor == 0)
			{
				int nExp = tc_engine::exportAllModified();
				setStatus("Exported " + std::to_string(nExp) + " file(s) to export/");
			}
			else if (g_st.cursor == 1)
			{
				int nSav = tc_engine::saveAllModified();
				setStatus("Saved " + std::to_string(nSav) + " file(s) with backup");
			}
			else if (g_st.cursor == 2)
			{
				tc_engine::requestFullCycleReload();
				setStatus("Reloaded weather cycles");
			}
			else if (g_st.cursor == 3)
			{
				tc_engine::discardAllLive();
				setStatus("Discarded live changes");
			}
		}
	}

	void drawMenu(const std::string& title, const std::string& subtitle, const std::vector<RowItem>& items)
	{
		const auto& cfg = Config::get();
		const float x = cfg.menuX;
		const float y = cfg.menuY;
		const float w = cfg.menuWidth;
		const int ar = cfg.accentR;
		const int ag = cfg.accentG;
		const int ab = cfg.accentB;
		const int bgA = cfg.bgAlpha;
		const bool darkAccent = (0.299f * ar + 0.587f * ag + 0.114f * ab) < 90.0f;
		const int atr = darkAccent ? 235 : ar;
		const int atg = darkAccent ? 240 : ag;
		const int atb = darkAccent ? 248 : ab;

		const float accentH = 0.0025f;
		const float headerH = 0.052f;
		const float subH    = 0.026f;
		const float rowH    = 0.028f;
		const float footerH = 0.026f;

		const int total   = (int)items.size();
		const int maxVis  = cfg.maxVisibleRows;
		const int visCnt  = std::min(total, maxVis);
		int startIdx = 0;
		if (total > maxVis)
		{
			startIdx = std::clamp(g_st.cursor - maxVis / 2, 0, total - maxVis);
		}

		const float totalH = accentH + headerH + subH + (float)visCnt * rowH + footerH;

		// Crisp 1px-style outer border
		drawRectTL(x - 0.0012f, y - 0.0020f, w + 0.0024f, totalH + 0.0040f, 45, 50, 62, 255);

		// Top accent bar
		drawRectTL(x, y, w, accentH, ar, ag, ab, 255);

		// Header
		const float hdrY = y + accentH;
		drawRectTL(x, hdrY, w, headerH, 9, 10, 13, std::min(255, bgA + 12));
		drawText(title, x + 0.008f, hdrY + 0.007f, 0.33f, 244, 244, 245, 255, 0);
		drawText(std::string("Mod v") + updater::kModVersion,
			x + 0.008f, hdrY + 0.029f, 0.21f, atr, atg, atb, 235, 0);
		drawText("Made by sensin", x, hdrY + 0.008f, 0.22f, 203, 213, 225, 255, 0, true, x + w - 0.008f);
		drawText("akasensin on github", x, hdrY + 0.026f, 0.20f, 148, 163, 184, 255, 0, true, x + w - 0.008f);

		// Subtitle bar
		const float subY = hdrY + headerH;
		drawRectTL(x, subY, w, subH, 16, 18, 24, std::min(255, bgA + 6));
		drawText(subtitle, x + 0.008f, subY + 0.004f, 0.26f, 148, 163, 184, 255, 0);
		if (total > 0)
		{
			char cntBuf[32];
			std::snprintf(cntBuf, sizeof(cntBuf), "%d / %d", g_st.cursor + 1, total);
			drawText(cntBuf, x, subY + 0.004f, 0.26f, atr, atg, atb, 255, 0, true, x + w - 0.008f);
		}
		drawRectTL(x, subY + subH - 0.0012f, w, 0.0012f, 36, 40, 50, 255);

		// Rows
		const bool hasScroll = (total > maxVis && visCnt > 0);
		const float contentRightPad = hasScroll ? 0.013f : 0.008f;
		float rowY = subY + subH;
		for (int i = 0; i < visCnt; ++i)
		{
			const int idx = startIdx + i;
			const auto& item = items[idx];
			const bool active = (idx == g_st.cursor);

			if (active)
			{
				drawRectTL(x, rowY, w, rowH, 28, 33, 44, std::min(255, bgA + 15));
				drawRectTL(x, rowY, 0.0028f, rowH, ar, ag, ab, 255);
			}
			else
			{
				const int rowBg = (i % 2 == 0) ? 11 : 14;
				drawRectTL(x, rowY, w, rowH, rowBg, rowBg + 1, rowBg + 5, bgA);
			}

			const int tr = active ? 248 : (item.accent ? atr : 226);
			const int tg = active ? 250 : (item.accent ? atg : 230);
			const int tb = active ? 252 : (item.accent ? atb : 236);

			const int rr = active ? atr : tr;
			const int rg = active ? atg : tg;
			const int rb = active ? atb : tb;

			drawText(item.left, x + 0.008f, rowY + 0.0045f, 0.29f, tr, tg, tb, 255, 0);

			float rightMargin = x + w - contentRightPad;
			if (item.hasColor)
			{
				drawRectTL(rightMargin - 0.012f, rowY + 0.006f, 0.012f, rowH - 0.012f, item.cr, item.cg, item.cb, 255);
				rightMargin -= 0.016f;
			}
			if (!item.right.empty())
				drawText(item.right, x, rowY + 0.0045f, 0.29f, rr, rg, rb, 255, 0, true, rightMargin);

			rowY += rowH;
		}

		// Scrollbar track & thumb when list exceeds visible rows
		if (hasScroll)
		{
			const float listH = (float)visCnt * rowH;
			const float sbX = x + w - 0.0045f;
			const float sbY = subY + subH;
			drawRectTL(sbX, sbY, 0.0028f, listH, 22, 25, 33, 220);
			const float thumbH = std::max(0.014f, listH * ((float)visCnt / (float)total));
			const float frac   = (total > visCnt) ? ((float)startIdx / (float)(total - visCnt)) : 0.0f;
			const float thumbY = sbY + (listH - thumbH) * frac;
			drawRectTL(sbX, thumbY, 0.0028f, thumbH, ar, ag, ab, 230);
		}

		// Footer
		drawRectTL(x, rowY, w, 0.0012f, 36, 40, 50, 255);
		drawRectTL(x, rowY + 0.0012f, w, footerH - 0.0012f, 9, 10, 13, std::min(255, bgA + 12));
		const Page curP = currentPage();
		std::string foot = (curP == Page::CycleVarList || curP == Page::ModVarList)
			? ("[" + cfg.toggleKeyLabel + "] Close  [Enter] Input  [R/Del] Reset  [Shift] 10x")
			: ("[" + cfg.toggleKeyLabel + "] Close  [Arrows] Edit  [Enter] Select  [Back] Return");
		drawText(foot, x + 0.008f, rowY + 0.004f, 0.24f, 128, 138, 156, 255, 0);
	}
}

namespace ui_menu
{
	bool isOpen() { return g_st.open; }

	void onKeyMessage(DWORD key, WORD /*repeats*/, BYTE /*scanCode*/, BOOL /*isExtended*/, BOOL /*isWithAlt*/, BOOL wasDownBefore, BOOL isUpNow)
	{
		if (key >= 256) return;
		auto& k = g_keys[key];
		if (isUpNow)
		{
			k.down = false;
		}
		else if (!wasDownBefore)
		{
			k.down        = true;
			k.justPressed = true;
			k.pressedAt   = GetTickCount64();
			k.lastRepeat  = k.pressedAt;
		}
	}

	void tick()
	{
		static bool s_inited = false;
		if (!s_inited)
		{
			s_inited = true;
			g_st.stepSize = Config::get().stepDefault;
			syncThemePresetIdx();
		}

		// Keep time and weather locked every frame when enabled
		if (g_st.clockFrozen)
		{
			TIME::PAUSE_CLOCK(TRUE);
			TIME::SET_CLOCK_TIME(g_st.frozenHour, g_st.frozenMinute, 0);
		}
		if (g_st.weatherLocked && !g_st.lockedWeather.empty())
		{
			if (currentWeatherLabel() != g_st.lockedWeather)
				GAMEPLAY::SET_WEATHER_TYPE_NOW_PERSIST((char*)g_st.lockedWeather.c_str());
		}

		// Flush debounced cycle reload
		if (g_st.cycleReloadPending && GetTickCount64() >= g_st.cycleReloadAt)
		{
			g_st.cycleReloadPending = false;
			tc_engine::requestFullCycleReload();
		}

		// Toggle menu key
		if (!g_st.kbActive && consumeKey(Config::get().toggleKey))
		{
			g_st.open = !g_st.open;
			if (g_st.open)
				clearJustPressed();
			AUDIO::PLAY_SOUND_FRONTEND(-1,
				(char*)(g_st.open ? "SELECT" : "BACK"),
				(char*)"HUD_FRONTEND_DEFAULT_SOUNDSET", FALSE);
		}

		pollKeyboardInput();

		// Status toast
		if (!g_st.statusText.empty() && GetTickCount64() < g_st.statusUntil)
		{
			const auto& cfg = Config::get();
			const bool darkAccent = (0.299f * cfg.accentR + 0.587f * cfg.accentG + 0.114f * cfg.accentB) < 90.0f;
			drawRectTL(0.36f, 0.02f, 0.28f, 0.032f, 11, 12, 15, 238);
			drawRectTL(0.36f, 0.02f, 0.0028f, 0.032f, cfg.accentR, cfg.accentG, cfg.accentB, 255);
			drawText(g_st.statusText, 0.37f, 0.025f, 0.31f,
				darkAccent ? 235 : cfg.accentR,
				darkAccent ? 240 : cfg.accentG,
				darkAccent ? 248 : cfg.accentB, 255, 0);
		}

		if (!g_st.open)
		{
			clearJustPressed(Config::get().toggleKey);
			return;
		}

		// Disable conflicting frontend/combat actions while menu is open
		CONTROLS::DISABLE_CONTROL_ACTION(2, 27, TRUE);  // Phone
		CONTROLS::DISABLE_CONTROL_ACTION(2, 19, TRUE);  // Character wheel
		CONTROLS::DISABLE_CONTROL_ACTION(2, 172, TRUE); // Frontend Up
		CONTROLS::DISABLE_CONTROL_ACTION(2, 173, TRUE); // Frontend Down
		CONTROLS::DISABLE_CONTROL_ACTION(2, 174, TRUE); // Frontend Left
		CONTROLS::DISABLE_CONTROL_ACTION(2, 175, TRUE); // Frontend Right
		CONTROLS::DISABLE_CONTROL_ACTION(2, 176, TRUE); // Frontend Accept
		CONTROLS::DISABLE_CONTROL_ACTION(2, 177, TRUE); // Frontend Cancel

		std::string title, subtitle;
		std::vector<RowItem> items;
		buildRows(title, subtitle, items);

		if (g_st.kbActive)
		{
			clearJustPressed();
		}
		else
		{
			handleInput(items);
			if (!g_st.open) return;
			buildRows(title, subtitle, items);
		}

		drawMenu(title, subtitle, items);
	}
}
