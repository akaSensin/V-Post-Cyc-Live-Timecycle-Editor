#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <Windows.h>
#include <MinHook.h>
#include "log.h"

class memory
{
public:
	uintptr_t address;

	memory(const memory& other) : address(other.address) {}
	explicit memory(uintptr_t offset, bool useBase = false) : address(useBase ? base() + offset : offset) {}

	static uintptr_t& base()
	{
		static uintptr_t _base = 0;
		return _base;
	}

	static void init()
	{
		base() = (uintptr_t)GetModuleHandleA(nullptr);
		auto status = MH_Initialize();
		if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED)
			logger::write("error", "MH_Initialize failed: %d", (int)status);
	}

	memory add(uintptr_t offset) const { return memory(address + offset); }

	memory rip() const
	{
		return memory(address + *(int32_t*)address + 4);
	}

	template <class T>
	bool hook(T* target, T** orig = nullptr) const
	{
		auto st = MH_CreateHook((void*)address, (void*)target, (void**)orig);
		if (st != MH_OK)
		{
			logger::write("error", "MH_CreateHook(%p) failed: %d", (void*)address, (int)st);
			return false;
		}
		st = MH_EnableHook((void*)address);
		if (st != MH_OK)
		{
			logger::write("error", "MH_EnableHook(%p) failed: %d", (void*)address, (int)st);
			return false;
		}
		return true;
	}

	static bool HookApi(const wchar_t* module, const char* funcName, PVOID pNewFunc, PVOID* pOldFunc)
	{
		auto st = MH_CreateHookApi(module, funcName, pNewFunc, pOldFunc);
		if (st != MH_OK)
		{
			logger::write("error", "MH_CreateHookApi(%s) failed: %d", funcName, (int)st);
			return false;
		}
		st = MH_EnableHook(MH_ALL_HOOKS);
		if (st != MH_OK)
		{
			logger::write("error", "MH_EnableHook failed: %d", (int)st);
			return false;
		}
		return true;
	}

	static memory scan(const char* signature, bool ignoreFail = false)
	{
		if (!signature || !signature[0]) return memory(0);

		std::vector<int> patternBytes;
		const char* cur = signature;
		const char* end = signature + std::strlen(signature);
		while (cur < end)
		{
			if (*cur == ' ') { ++cur; continue; }
			if (*cur == '?')
			{
				++cur;
				if (cur < end && *cur == '?') ++cur;
				patternBytes.push_back(-1);
			}
			else
			{
				char* next = nullptr;
				unsigned long val = std::strtoul(cur, &next, 16);
				if (next == cur) break;
				patternBytes.push_back((int)val);
				cur = next;
			}
		}

		if (patternBytes.empty()) return memory(0);

		const uintptr_t moduleBase = base();
		auto* dosHeader = (PIMAGE_DOS_HEADER)moduleBase;
		auto* ntHeaders = (PIMAGE_NT_HEADERS)((uint8_t*)moduleBase + dosHeader->e_lfanew);
		const size_t sizeOfImage = ntHeaders->OptionalHeader.SizeOfImage;
		const auto* scanBytes = (const uint8_t*)moduleBase;

		const size_t s = patternBytes.size();
		const int*   d = patternBytes.data();

		for (size_t i = 0; i + s <= sizeOfImage; ++i)
		{
			bool found = true;
			for (size_t j = 0; j < s; ++j)
			{
				if (d[j] != -1 && scanBytes[i + j] != (uint8_t)d[j])
				{
					found = false;
					break;
				}
			}
			if (found)
				return memory((uintptr_t)&scanBytes[i]);
		}

		if (!ignoreFail)
			logger::write("warn", "Pattern not found: %s", signature);
		return memory(0);
	}
};
