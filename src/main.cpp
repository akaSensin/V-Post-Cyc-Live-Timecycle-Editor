#include "core/tc_engine.h"
#include "game/game.h"
#include "ui/menu.h"
#include "utils/config.h"
#include "utils/log.h"
#include "utils/memory.h"

#include <main.h>
#include <Windows.h>
#include <filesystem>
#include <mutex>

namespace
{
	std::once_flag g_initOnce;
	void (*origGetSystemTimeAsFileTime)(LPFILETIME) = nullptr;

	void WINAPI hkGetSystemTimeAsFileTime(LPFILETIME lpSystemTimeAsFileTime)
	{
		std::call_once(g_initOnce, []()
		{
			if (!game::isSupportedGame())
			{
				logger::write("info", "Unsupported exe '%s' - hooks dormant", game::exeName());
			}
			else
			{
				game::resolve();
				tc_engine::installHooks();
			}
		});
		origGetSystemTimeAsFileTime(lpSystemTimeAsFileTime);
	}

	void ScriptMain()
	{
		tc_engine::loadDocuments();
		while (true)
		{
			ui_menu::tick();
			WAIT(0);
		}
	}
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(hModule);

		Config::get().load(hModule);
		logger::init((std::filesystem::path(Config::get().moduleDir) / "TimecycleEditor.log").string());
		logger::write("info", "TimecycleEditor attached (toggleKey=%s, vk=0x%X)",
			Config::get().toggleKeyLabel.c_str(), Config::get().toggleKey);

		memory::init();
		if (!memory::HookApi(L"kernel32.dll", "GetSystemTimeAsFileTime",
			(PVOID)hkGetSystemTimeAsFileTime, (PVOID*)&origGetSystemTimeAsFileTime))
		{
			logger::write("warn", "GetSystemTimeAsFileTime hook failed (%lu)", GetLastError());
		}

		scriptRegister(hModule, ScriptMain);
		keyboardHandlerRegister(ui_menu::onKeyMessage);
	}
	else if (reason == DLL_PROCESS_DETACH)
	{
		keyboardHandlerUnregister(ui_menu::onKeyMessage);
		scriptUnregister(hModule);
		if (reserved == nullptr)
		{
			MH_DisableHook(MH_ALL_HOOKS);
			MH_Uninitialize();
		}
	}
	return TRUE;
}
