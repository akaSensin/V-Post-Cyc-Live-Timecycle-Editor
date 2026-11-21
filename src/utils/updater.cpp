#include "updater.h"
#include "log.h"

#include <Windows.h>
#include <shellapi.h>
#include <winhttp.h>

#include <atomic>
#include <cctype>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <tuple>

namespace
{
	std::atomic<updater::Status> g_status{ updater::Status::Idle };
	std::mutex                   g_mx;
	std::string                  g_latestTag;

	std::tuple<int, int, int> parseSemver(const std::string& s)
	{
		int nums[3] = { 0, 0, 0 };
		int idx = 0;
		size_t i = 0;
		while (i < s.size() && idx < 3)
		{
			if (std::isdigit((unsigned char)s[i]))
			{
				char* end = nullptr;
				nums[idx++] = (int)std::strtol(s.c_str() + i, &end, 10);
				i = (size_t)(end - s.c_str());
			}
			else
			{
				++i;
			}
		}
		return { nums[0], nums[1], nums[2] };
	}

	std::string extractJsonString(const std::string& json, const char* key)
	{
		std::string pat = std::string("\"") + key + "\"";
		size_t p = json.find(pat);
		if (p == std::string::npos) return {};
		p = json.find(':', p + pat.size());
		if (p == std::string::npos) return {};
		p = json.find('"', p + 1);
		if (p == std::string::npos) return {};
		size_t end = json.find('"', p + 1);
		if (end == std::string::npos) return {};
		return json.substr(p + 1, end - (p + 1));
	}

	void runCheck()
	{
		HINTERNET hSession = WinHttpOpen(
			L"TimecycleEditor/1.0.0",
			WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
			WINHTTP_NO_PROXY_NAME,
			WINHTTP_NO_PROXY_BYPASS,
			0);
		if (!hSession)
		{
			g_status.store(updater::Status::Error);
			return;
		}
		WinHttpSetTimeouts(hSession, 5000, 5000, 5000, 8000);

		HINTERNET hConnect = WinHttpConnect(hSession, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
		HINTERNET hRequest = hConnect
			? WinHttpOpenRequest(hConnect, L"GET",
				L"/repos/akaSensin/V-Post-Cyc-Live-Timecycle-Editor/releases/latest",
				nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
			: nullptr;

		std::string body;
		DWORD statusCode = 0;
		bool ok = false;

		if (hRequest &&
			WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
			WinHttpReceiveResponse(hRequest, nullptr))
		{
			DWORD sz = sizeof(statusCode);
			WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
				nullptr, &statusCode, &sz, WINHTTP_NO_HEADER_INDEX);

			if (statusCode == 200)
			{
				DWORD avail = 0;
				while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0)
				{
					std::string chunk(avail, '\0');
					DWORD read = 0;
					if (!WinHttpReadData(hRequest, chunk.data(), avail, &read) || read == 0)
						break;
					body.append(chunk.data(), read);
					if (body.size() > 262144) break;
				}
				ok = !body.empty();
			}
		}

		if (hRequest) WinHttpCloseHandle(hRequest);
		if (hConnect) WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);

		if (!ok)
		{
			logger::write("warn", "updater: check failed (http=%lu)", statusCode);
			g_status.store(updater::Status::Error);
			return;
		}

		std::string tag = extractJsonString(body, "tag_name");
		if (tag.empty())
		{
			g_status.store(updater::Status::Error);
			return;
		}

		{
			std::lock_guard<std::mutex> lk(g_mx);
			g_latestTag = tag;
		}

		auto latest = parseSemver(tag);
		auto current = parseSemver(updater::kModVersion);
		if (latest > current)
		{
			logger::write("info", "updater: update available (%s > v%s)", tag.c_str(), updater::kModVersion);
			g_status.store(updater::Status::UpdateAvailable);
		}
		else
		{
			logger::write("info", "updater: up to date (%s <= v%s)", tag.c_str(), updater::kModVersion);
			g_status.store(updater::Status::UpToDate);
		}
	}
}

namespace updater
{
	void checkAsync()
	{
		Status expected = g_status.load();
		if (expected == Status::Checking) return;
		g_status.store(Status::Checking);
		std::thread(runCheck).detach();
	}

	Status status()
	{
		return g_status.load();
	}

	std::string statusLabel()
	{
		switch (g_status.load())
		{
		case Status::Checking:        return "Checking...";
		case Status::UpToDate:        return std::string("Up to date (v") + kModVersion + ")";
		case Status::UpdateAvailable:
		{
			std::lock_guard<std::mutex> lk(g_mx);
			return "New: " + g_latestTag;
		}
		case Status::Error:           return "Check failed";
		case Status::Idle:
		default:                      return std::string("v") + kModVersion + " [Check]";
		}
	}

	void openReleasesPage()
	{
		ShellExecuteA(nullptr, "open", kReleasesUrl, nullptr, nullptr, SW_SHOWNORMAL);
	}

	void openGithubPage()
	{
		ShellExecuteA(nullptr, "open", kGithubProfile, nullptr, nullptr, SW_SHOWNORMAL);
	}
}
