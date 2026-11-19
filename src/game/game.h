#pragma once
#include <cstdint>

namespace game
{
	extern uintptr_t addr_FrameTick;
	extern uintptr_t addr_TcCycle_Load;
	extern uintptr_t addr_TimeCycle_Init;
	extern uintptr_t addr_g_timeCycle;
	extern uintptr_t addr_LoadModifierFile;
	extern uintptr_t addr_RevertModifierFile;
	extern uintptr_t addr_FinishModifierLoad;

	bool isEnhanced();
	bool isSupportedGame();
	const char* exeName();
	void resolve();
}
