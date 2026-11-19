#pragma once
#include <string>

namespace logger
{
	void init(const std::string& logPath);
	void write(const char* tag, const char* fmt, ...);
}
