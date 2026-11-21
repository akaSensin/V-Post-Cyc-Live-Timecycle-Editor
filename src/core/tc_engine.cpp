#include "tc_engine.h"
#include "../game/game.h"
#include "../utils/config.h"
#include "../utils/log.h"
#include "../utils/memory.h"

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <climits>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>

namespace fs = std::filesystem;

namespace
{
	using TcLoadFn = void(__fastcall*)(void* cycle, const char* filename);
	using ModFn    = void(__fastcall*)(void* thiz, const char* filename);
	using FinishFn = void(__fastcall*)(void* thiz);
	using InitFn   = void(__fastcall*)(void* thiz, int mode);
	using TickFn   = void(__fastcall*)(void* a, void* b, void* c, void* d);

	TcLoadFn oTcLoad  = nullptr;
	ModFn    oLoadMod = nullptr;
	TickFn   oTick    = nullptr;

	bool g_hooksInstalled = false;
	bool g_marshalReady   = false;

	enum ReloadBits : uint32_t
	{
		RK_Cycles = 1u << 0,
		RK_Mods   = 1u << 1,
	};

	std::atomic<uint32_t> g_pending{ 0 };
	std::mutex            g_patchMx;
	std::vector<tc_engine::ModPatch> g_pendingPatches;

	std::vector<XmlDoc> g_cycleDocs;
	std::vector<XmlDoc> g_modDocs;

	std::string baseName(const char* path)
	{
		std::string s(path ? path : "");
		size_t p = s.find_last_of("/\\");
		return p == std::string::npos ? s : s.substr(p + 1);
	}

	std::string toLower(std::string s)
	{
		for (auto& c : s) c = (char)std::tolower((unsigned char)c);
		return s;
	}

	uint32_t joaat(const std::string& s)
	{
		uint32_t h = 0;
		for (unsigned char c : s)
		{
			h += (uint8_t)std::tolower(c);
			h += h << 10;
			h ^= h >> 6;
		}
		h += h << 3;
		h ^= h >> 11;
		h += h << 15;
		return h;
	}

	const char* redirectPath(const char* filename, std::string& storage, const char* tag)
	{
		if (!filename || !Config::get().redirectFromFolder)
			return filename;

		std::string name = baseName(filename);
		if (name.empty()) return filename;

		std::error_code ec;
		fs::path liveP = fs::path(Config::get().liveDir) / name;
		if (fs::exists(liveP, ec))
		{
			storage = liveP.string();
			return storage.c_str();
		}

		fs::path dirP = fs::path(Config::get().timecycleDir) / name;
		if (fs::exists(dirP, ec))
		{
			storage = dirP.string();
			logger::write("info", "%s redirect '%s' -> '%s'", tag, filename, storage.c_str());
			return storage.c_str();
		}
		return filename;
	}

	void __fastcall hkTcLoad(void* cycle, const char* filename)
	{
		thread_local std::string swap;
		oTcLoad(cycle, redirectPath(filename, swap, "cycle"));
	}

	void __fastcall hkLoadMod(void* thiz, const char* filename)
	{
		thread_local std::string swap;
		oLoadMod(thiz, redirectPath(filename, swap, "modifier"));
	}

	// --- In-place tcModifier patching (from fxReloader tc_patch) ------------
	bool canRead(const void* p, size_t n)
	{
		if (!p) return false;
		MEMORY_BASIC_INFORMATION mbi{};
		if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
		if (mbi.State != MEM_COMMIT) return false;
		if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
		return (uint8_t*)mbi.BaseAddress + mbi.RegionSize >= (uint8_t*)p + n;
	}

#pragma pack(push, 1)
	struct AtArrayRaw { uint64_t ptr; uint16_t count; uint16_t cap; uint32_t pad; };
	struct TcModData  { int32_t index; float v1; float v2; };
#pragma pack(pop)

	constexpr uint32_t kNameHashOff  = 0x10;
	constexpr float    kSentinelBase = 90000.0f;

	uintptr_t g_modArrayAddr = 0;
	std::unordered_map<std::string, int> g_varIds;

	bool validModArrayAt(uintptr_t addr, uint32_t wantTailHash, uintptr_t* tailOut)
	{
		if (!canRead((void*)addr, sizeof(AtArrayRaw))) return false;
		AtArrayRaw a = *(AtArrayRaw*)addr;
		if (!a.ptr || a.count == 0 || a.count > a.cap || a.cap > 60000) return false;
		if (!canRead((void*)a.ptr, sizeof(uint64_t) * a.count)) return false;
		uintptr_t tail = (uintptr_t)((uint64_t*)a.ptr)[a.count - 1];
		if (!canRead((void*)tail, kNameHashOff + 4)) return false;
		if (*(uint32_t*)(tail + kNameHashOff) != wantTailHash) return false;
		if (tailOut) *tailOut = tail;
		return true;
	}

	bool probeVars(const std::vector<std::string>& names)
	{
		if (!game::addr_g_timeCycle || !game::addr_LoadModifierFile ||
			!game::addr_RevertModifierFile || !game::addr_FinishModifierLoad)
			return false;

		void* tc     = (void*)game::addr_g_timeCycle;
		auto  load   = (ModFn)game::addr_LoadModifierFile;
		auto  revert = (ModFn)game::addr_RevertModifierFile;
		auto  finish = (FinishFn)game::addr_FinishModifierLoad;

		const std::string path = (fs::path(Config::get().moduleDir) / "tce_probe_tmp.xml").string();
		{
			std::ofstream o(path, std::ios::binary | std::ios::trunc);
			if (!o) return false;
			o << "<?xml version='1.0' encoding='UTF-8'?>\n"
			     "<timecycle_modifier_data version=\"1.000000\">\n"
			     "  <modifier name=\"tceprobe\" numMods=\"" << names.size() << "\" userFlags=\"0\">\n";
			char buf[64];
			for (size_t k = 0; k < names.size(); ++k)
			{
				std::snprintf(buf, sizeof(buf), "%.1f", kSentinelBase + (float)k);
				o << "    <" << names[k] << ">" << buf << " " << buf << "</" << names[k] << ">\n";
			}
			o << "  </modifier>\n</timecycle_modifier_data>\n";
		}

		load(tc, path.c_str());
		finish(tc);

		const uint32_t probeHash = joaat("tceprobe");
		uintptr_t probeMod = 0;
		if (g_modArrayAddr)
		{
			validModArrayAt(g_modArrayAddr, probeHash, &probeMod);
		}
		else
		{
			for (uintptr_t off = 0; off <= 0x400; off += 8)
			{
				if (validModArrayAt(game::addr_g_timeCycle + off, probeHash, &probeMod))
				{
					g_modArrayAddr = game::addr_g_timeCycle + off;
					logger::write("info", "tc_engine: modifier array at g_timeCycle+0x%llx", (uint64_t)off);
					break;
				}
			}
		}

		bool ok = false;
		if (probeMod)
		{
			AtArrayRaw md = *(AtArrayRaw*)probeMod;
			if (md.count > 0 && md.count <= names.size() && canRead((void*)md.ptr, md.count * sizeof(TcModData)))
			{
				const auto* e = (const TcModData*)md.ptr;
				std::vector<int> ids(names.size(), INT_MIN);
				for (uint32_t i = 0; i < md.count; ++i)
				{
					int k = (int)std::lround(e[i].v1 - kSentinelBase);
					if (k >= 0 && k < (int)names.size() && ids[k] == INT_MIN)
					{
						ids[k] = e[i].index;
					}
				}
				for (size_t k = 0; k < names.size(); ++k)
				{
					if (ids[k] != INT_MIN)
					{
						g_varIds[names[k]] = ids[k];
						ok = true;
					}
				}
			}
		}

		revert(tc, path.c_str());
		finish(tc);
		DeleteFileA(path.c_str());
		return ok;
	}

	void applyPatchesSimThread(const std::vector<tc_engine::ModPatch>& patches)
	{
		if (patches.empty() || !game::addr_g_timeCycle) return;

		std::vector<std::string> need;
		for (const auto& p : patches)
		{
			if (!g_varIds.count(p.var) &&
				std::find(need.begin(), need.end(), p.var) == need.end())
				need.push_back(p.var);
		}
		if (!need.empty() || !g_modArrayAddr)
		{
			if (need.empty()) need.push_back(patches[0].var);
			if (!probeVars(need)) return;
		}

		if (!canRead((void*)g_modArrayAddr, sizeof(AtArrayRaw))) return;
		AtArrayRaw a = *(AtArrayRaw*)g_modArrayAddr;
		if (!a.ptr || !canRead((void*)a.ptr, sizeof(uint64_t) * a.count)) return;

		std::unordered_map<uint32_t, uintptr_t> modByHash;
		modByHash.reserve(a.count);
		for (uint32_t i = 0; i < a.count; ++i)
		{
			uintptr_t m = (uintptr_t)((uint64_t*)a.ptr)[i];
			if (m && canRead((void*)m, kNameHashOff + 4))
				modByHash.emplace(*(uint32_t*)(m + kNameHashOff), m);
		}

		for (const auto& p : patches)
		{
			auto idIt = g_varIds.find(p.var);
			if (idIt == g_varIds.end()) continue;

			auto modIt = modByHash.find(joaat(p.modifier));
			if (modIt == modByHash.end()) continue;

			AtArrayRaw md = *(AtArrayRaw*)modIt->second;
			if (!md.ptr || !canRead((void*)md.ptr, md.count * sizeof(TcModData))) continue;

			auto* e = (TcModData*)md.ptr;
			for (uint32_t i = 0; i < md.count; ++i)
			{
				if (e[i].index == idIt->second)
				{
					e[i].v1 = p.v1;
					e[i].v2 = p.v2;
					break;
				}
			}
		}
	}

	void drainPending()
	{
		uint32_t kinds = g_pending.exchange(0, std::memory_order_acq_rel);
		if (!kinds) return;

		if ((kinds & RK_Cycles) && game::addr_TimeCycle_Init && game::addr_g_timeCycle)
		{
			((InitFn)game::addr_TimeCycle_Init)((void*)game::addr_g_timeCycle, 4);
		}

		if (kinds & RK_Mods)
		{
			std::vector<tc_engine::ModPatch> patches;
			{
				std::lock_guard<std::mutex> lk(g_patchMx);
				patches.swap(g_pendingPatches);
			}
			applyPatchesSimThread(patches);
		}
	}

	void __fastcall hkTick(void* a, void* b, void* c, void* d)
	{
		if (!game::addr_g_timeCycle && !game::isEnhanced() && a)
			game::addr_g_timeCycle = (uintptr_t)a;

		oTick(a, b, c, d);
		if (g_pending.load(std::memory_order_acquire))
			drainPending();
	}
}

namespace tc_engine
{
	void installHooks()
	{
		if (Config::get().redirectFromFolder)
		{
			bool tcOk = game::addr_TcCycle_Load && memory(game::addr_TcCycle_Load).hook(&hkTcLoad, &oTcLoad);
			if (game::addr_LoadModifierFile)
				memory(game::addr_LoadModifierFile).hook(&hkLoadMod, &oLoadMod);
			g_hooksInstalled = tcOk;
		}
		if (game::addr_FrameTick)
		{
			g_marshalReady = memory(game::addr_FrameTick).hook(&hkTick, &oTick);
		}
		logger::write("info", "tc_engine: hooks=%d marshal=%d", (int)g_hooksInstalled, (int)g_marshalReady);
	}

	void loadDocuments()
	{
		g_cycleDocs.clear();
		g_modDocs.clear();

		std::error_code ec;
		fs::path tcDir(Config::get().timecycleDir);
		if (!fs::exists(tcDir, ec))
		{
			logger::write("warn", "tc_engine: timecycleDir '%s' does not exist", tcDir.string().c_str());
			return;
		}

		std::vector<fs::path> cycleFiles, modFiles;
		for (auto& e : fs::directory_iterator(tcDir, ec))
		{
			if (ec) break;
			if (!e.is_regular_file(ec)) continue;
			std::string fn = toLower(e.path().filename().string());
			if (fn.size() <= 4 || fn.compare(fn.size() - 4, 4, ".xml") != 0) continue;
			if (fn.rfind("w_", 0) == 0)
				cycleFiles.push_back(e.path());
			else if (fn.rfind("timecycle_mods", 0) == 0)
				modFiles.push_back(e.path());
		}

		std::sort(cycleFiles.begin(), cycleFiles.end());
		std::sort(modFiles.begin(), modFiles.end());

		for (const auto& p : cycleFiles)
		{
			XmlDoc doc;
			if (doc.load(p.string()))
				g_cycleDocs.push_back(std::move(doc));
		}
		for (const auto& p : modFiles)
		{
			XmlDoc doc;
			if (doc.load(p.string()))
				g_modDocs.push_back(std::move(doc));
		}

		logger::write("info", "tc_engine: loaded %zu cycle file(s), %zu modifier file(s)",
			g_cycleDocs.size(), g_modDocs.size());
	}

	std::vector<XmlDoc>& cycleDocs() { return g_cycleDocs; }
	std::vector<XmlDoc>& modDocs()   { return g_modDocs; }

	const char* weatherNameForFilename(const std::string& filename)
	{
		std::string low = toLower(filename);
		if (low == "w_clear.xml")         return "CLEAR";
		if (low == "w_clearing.xml")      return "CLEARING";
		if (low == "w_clouds.xml")        return "CLOUDS";
		if (low == "w_extrasunny.xml")    return "EXTRASUNNY";
		if (low == "w_foggy.xml")         return "FOGGY";
		if (low == "w_halloween.xml")     return "HALLOWEEN";
		if (low == "w_neutral.xml")       return "NEUTRAL";
		if (low == "w_overcast.xml")      return "OVERCAST";
		if (low == "w_rain.xml")          return "RAIN";
		if (low == "w_rainhalloween.xml") return "RAIN_HALLOWEEN";
		if (low == "w_smog.xml")          return "SMOG";
		if (low == "w_snowhalloween.xml") return "SNOW_HALLOWEEN";
		if (low == "w_thunder.xml")       return "THUNDER";
		if (low == "w_xmas.xml")          return "XMAS";
		return nullptr;
	}

	const char* weatherNameForHash(uint32_t hash)
	{
		static constexpr const char* kWeathers[] = {
			"CLEAR", "EXTRASUNNY", "CLOUDS", "OVERCAST", "RAIN",
			"CLEARING", "THUNDER", "SMOG", "FOGGY", "XMAS",
			"SNOW", "SNOWLIGHT", "BLIZZARD", "NEUTRAL",
			"HALLOWEEN", "RAIN_HALLOWEEN", "SNOW_HALLOWEEN"
		};
		for (const char* w : kWeathers)
		{
			if (joaat(w) == hash)
				return w;
		}
		return nullptr;
	}

	int findCycleIndexByWeatherHash(uint32_t hash)
	{
		for (size_t i = 0; i < g_cycleDocs.size(); ++i)
		{
			const char* wn = weatherNameForFilename(g_cycleDocs[i].filename);
			if (wn && joaat(wn) == hash)
				return (int)i;
		}
		return -1;
	}

	void applyCycleLive(size_t docIdx)
	{
		if (docIdx >= g_cycleDocs.size()) return;
		g_cycleDocs[docIdx].writeLiveCache(Config::get().liveDir);
		g_pending.fetch_or(RK_Cycles, std::memory_order_acq_rel);
	}

	void applyModifierLive(const std::string& modifier, const std::string& var, float v1, float v2)
	{
		{
			std::lock_guard<std::mutex> lk(g_patchMx);
			for (auto& p : g_pendingPatches)
			{
				if (p.modifier == modifier && p.var == var)
				{
					p.v1 = v1;
					p.v2 = v2;
					g_pending.fetch_or(RK_Mods, std::memory_order_acq_rel);
					return;
				}
			}
			g_pendingPatches.push_back({ modifier, var, v1, v2 });
		}
		g_pending.fetch_or(RK_Mods, std::memory_order_acq_rel);
	}

	void requestFullCycleReload()
	{
		g_pending.fetch_or(RK_Cycles, std::memory_order_acq_rel);
	}

	bool exportDoc(XmlDoc& doc)
	{
		return doc.exportTo(Config::get().exportDir);
	}

	int exportAllModified()
	{
		int count = 0;
		for (auto& d : g_cycleDocs)
			if (d.dirty && d.exportTo(Config::get().exportDir)) ++count;
		for (auto& d : g_modDocs)
			if (d.dirty && d.exportTo(Config::get().exportDir)) ++count;
		return count;
	}

	bool saveDoc(XmlDoc& doc)
	{
		bool ok = doc.saveWithBackup(Config::get().backupDir);
		if (ok)
		{
			std::error_code ec;
			fs::remove(fs::path(Config::get().liveDir) / doc.filename, ec);
		}
		return ok;
	}

	int saveAllModified()
	{
		int count = 0;
		for (auto& d : g_cycleDocs)
			if (d.dirty && saveDoc(d)) ++count;
		for (auto& d : g_modDocs)
			if (d.dirty && saveDoc(d)) ++count;
		return count;
	}

	void discardAllLive()
	{
		std::error_code ec;
		fs::remove_all(Config::get().liveDir, ec);
		for (auto& d : g_cycleDocs) d.resetAll();
		for (auto& d : g_modDocs)
		{
			if (d.dirty)
			{
				for (const auto& r : d.rows)
				{
					if (r.dirty && r.orig.size() >= 2)
						applyModifierLive(r.group, r.name, r.orig[0], r.orig[1]);
				}
				d.resetAll();
			}
		}
		requestFullCycleReload();
	}

	bool isHookReady()    { return g_hooksInstalled; }
	bool isMarshalReady() { return g_marshalReady; }
}
