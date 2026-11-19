#pragma once
#include <Windows.h>

namespace ui_menu
{
	void onKeyMessage(DWORD key, WORD repeats, BYTE scanCode, BOOL isExtended, BOOL isWithAlt, BOOL wasDownBefore, BOOL isUpNow);
	void tick();
	bool isOpen();
}
