#pragma once

// Dual-build (GTA5_Enhanced.exe + legacy GTA5.exe) IDA-style byte patterns.
// Matched in-process against the live decrypted module via memory::scan().
namespace gsig
{
	struct Sig    { const char* enh; const char* leg; };
	struct IntSig { int enh; int leg; };

	// Legacy only: CVisualSettings::LoadAll's first fan-out is LoadVisualSettings(&g_timeCycle)
	// with `lea rcx,[g_timeCycle]` at LoadAll + 0x1F.
	inline constexpr Sig VISUALSETTINGS_LOADALL = {
		"53 48 83 EC 20 48 8D 0D ? ? ? ? 48 8D 15 ? ? ? ? E8 ? ? ? ? 89 C3 84 C0 0F 84",
		"48 83 EC 28 48 8D 15 ? ? ? ? 48 8D 0D ? ? ? ? E8 ? ? ? ? 84 C0 0F 84"
	};
	inline constexpr int LOADALL_GTIMECYCLE_LEA_OFF_LEG = 0x1F;

	// tcCycle::Load(cycle, const char* filename)
	inline constexpr Sig TCCYCLE_LOAD = {
		"41 57 41 56 41 55 41 54 56 57 55 53 48 83 EC 58 48 89 4C 24 38 48 8B 0D",
		"48 89 5C 24 10 48 89 4C 24 08 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 40 48 8B F1 48 8B 0D"
	};

	// CTimeCycle::Init(this, int mode) — mode=4 reloads all w_*.xml weather cycles
	inline constexpr Sig TIMECYCLE_INIT = {
		"41 57 41 56 41 55 41 54 56 57 55 53 48 81 EC 18 09 00 00 0F 29 BC 24 00 09 00 00",
		"48 8B C4 48 89 58 20 55 56 57 41 56 41 57 48 8D A8 C8 F7 FF FF"
	};

	// tcManager::LoadModifierFile(this, const char*)
	inline constexpr Sig TCMANAGER_LOADMODIFIERFILE = {
		"41 57 41 56 41 55 41 54 56 57 55 53 48 83 EC 58 49 89 CF 48 8B 0D",
		"48 89 5C 24 08 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 20 4C 8B F9 48 8B 0D"
	};

	// tcManager::RevertModifierFile(this, const char*)
	inline constexpr Sig TCMANAGER_REVERTMODIFIERFILE = {
		"41 57 41 56 41 55 41 54 56 57 55 53 48 83 EC 38 49 89 CD 48 8B 0D",
		"48 89 5C 24 08 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 20 4C 8B F1 48 8B 0D"
	};

	// tcManager::FinishModifierLoad(this)
	inline constexpr Sig TCMANAGER_FINISHMODIFIERLOAD = {
		"56 57 48 83 EC 28 48 89 CE 80 79 50 00 75 28 44 0F B7 46 60",
		"48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 33 F6 48 8B D9 40 38 71 50"
	};

	// Sim-thread per-frame marshal:
	// Enh: CTimeCycle::Update wrapper (`lea rcx,[g_timeCycle]; xor edx,edx; jmp Update; nop`)
	// Leg: CTimeCycle::Update method directly (`this` in RCX = g_timeCycle)
	inline constexpr Sig FRAME_TICK = {
		"48 8D 0D ? ? ? ? 31 D2 E9 02 00 00 00 90",
		"48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 33 F6 48 8B D9 88 91 B0 08 00 00 84 D2"
	};
}
