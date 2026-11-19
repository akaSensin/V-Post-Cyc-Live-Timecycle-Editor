#include "game.h"
#include "signatures.h"
#include "../utils/memory.h"
#include "../utils/log.h"

#include <Windows.h>
#include <cstring>

namespace game
{
	uintptr_t addr_FrameTick          = 0;
	uintptr_t addr_TcCycle_Load       = 0;
	uintptr_t addr_TimeCycle_Init     = 0;
	uintptr_t addr_g_timeCycle        = 0;
	uintptr_t addr_LoadModifierFile   = 0;
	uintptr_t addr_RevertModifierFile = 0;
	uintptr_t addr_FinishModifierLoad = 0;

	const char* exeName()
	{
		static char name[MAX_PATH]{};
		if (!name[0])
		{
			char full[MAX_PATH]{};
			GetModuleFileNameA(GetModuleHandleA(nullptr), full, MAX_PATH);
			const char* b = std::strrchr(full, '\\');
			strncpy_s(name, b ? b + 1 : full, _TRUNCATE);
		}
		return name;
	}

	bool isEnhanced()
	{
		static const bool enh = _stricmp(exeName(), "GTA5_Enhanced.exe") == 0;
		return enh;
	}

	bool isSupportedGame()
	{
		const char* fn = exeName();
		return _stricmp(fn, "GTA5.exe") == 0 || _stricmp(fn, "GTA5_Enhanced.exe") == 0;
	}

	static uintptr_t resolveOne(const char* name, const gsig::Sig& s)
	{
		const char* pat = isEnhanced() ? s.enh : s.leg;
		if (!pat || !pat[0]) return 0;
		uintptr_t a = memory::scan(pat, true).address;
		if (a) logger::write("info", "sig %-22s : +0x%llx", name, (uint64_t)(a - memory::base()));
		else   logger::write("warn", "sig %-22s : NOT FOUND", name);
		return a;
	}

	static uintptr_t deriveLeaTarget(uintptr_t leaAddr)
	{
		if (!leaAddr) return 0;
		const auto* b = (const uint8_t*)leaAddr;
		if (b[0] == 0x48 && b[1] == 0x8D && (b[2] == 0x0D || b[2] == 0x15))
			return memory(leaAddr + 3).rip().address;
		return 0;
	}

	void resolve()
	{
		const bool enh = isEnhanced();
		logger::write("info", "build = %s (base=%p)", exeName(), (void*)memory::base());

		addr_TcCycle_Load       = resolveOne("tcCycle::Load",      gsig::TCCYCLE_LOAD);
		addr_TimeCycle_Init     = resolveOne("CTimeCycle::Init",   gsig::TIMECYCLE_INIT);
		addr_LoadModifierFile   = resolveOne("LoadModifierFile",   gsig::TCMANAGER_LOADMODIFIERFILE);
		addr_RevertModifierFile = resolveOne("RevertModifierFile", gsig::TCMANAGER_REVERTMODIFIERFILE);
		addr_FinishModifierLoad = resolveOne("FinishModifierLoad", gsig::TCMANAGER_FINISHMODIFIERLOAD);
		addr_FrameTick          = resolveOne("FrameTick",          gsig::FRAME_TICK);

		if (enh)
		{
			addr_g_timeCycle = deriveLeaTarget(addr_FrameTick);
		}
		else
		{
			uintptr_t vsLoadAll = resolveOne("VisualSettings::LoadAll", gsig::VISUALSETTINGS_LOADALL);
			if (vsLoadAll)
				addr_g_timeCycle = deriveLeaTarget(vsLoadAll + gsig::LOADALL_GTIMECYCLE_LEA_OFF_LEG);
		}

		if (addr_g_timeCycle)
			logger::write("info", "sig %-22s : +0x%llx (derived)", "g_timeCycle", (uint64_t)(addr_g_timeCycle - memory::base()));
		else
			logger::write("info", "g_timeCycle not yet derived%s", enh ? "" : " (will capture from Update)");
	}
}
