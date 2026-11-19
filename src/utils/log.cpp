#include "log.h"
#include <Windows.h>
#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace
{
	std::mutex  g_logMx;
	std::string g_logPath;
	FILE*       g_logFile = nullptr;
}

namespace logger
{
	void init(const std::string& logPath)
	{
		std::lock_guard<std::mutex> lk(g_logMx);
		g_logPath = logPath;
		if (g_logFile)
		{
			fclose(g_logFile);
			g_logFile = nullptr;
		}
		fopen_s(&g_logFile, g_logPath.c_str(), "w");
		if (g_logFile)
			setvbuf(g_logFile, nullptr, _IONBF, 0);
	}

	void write(const char* tag, const char* fmt, ...)
	{
		std::lock_guard<std::mutex> lk(g_logMx);
		if (!g_logFile) return;

		SYSTEMTIME st{};
		GetLocalTime(&st);
		std::fprintf(g_logFile, "[%02d:%02d:%02d.%03d] [%s] ",
			st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, tag ? tag : "info");

		va_list ap;
		va_start(ap, fmt);
		std::vfprintf(g_logFile, fmt, ap);
		va_end(ap);

		std::fputc('\n', g_logFile);
	}
}
