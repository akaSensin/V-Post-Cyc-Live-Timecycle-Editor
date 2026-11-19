#include "xml_doc.h"
#include "../utils/log.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace fs = std::filesystem;

namespace
{
	bool readFileBytes(const std::string& path, std::string& out)
	{
		std::ifstream in(path, std::ios::binary | std::ios::ate);
		if (!in) return false;
		auto sz = in.tellg();
		if (sz < 0) return false;
		out.resize((size_t)sz);
		in.seekg(0);
		if (sz > 0) in.read(&out[0], sz);
		return (bool)in;
	}

	bool writeFileBytes(const std::string& path, const std::string& data)
	{
		std::error_code ec;
		fs::create_directories(fs::path(path).parent_path(), ec);
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		if (!out) return false;
		out.write(data.data(), (std::streamsize)data.size());
		return (bool)out;
	}

	std::string extractAttr(const std::string& tagBody, const char* attrName)
	{
		std::string key = std::string(attrName) + "=";
		size_t pos = tagBody.find(key);
		if (pos == std::string::npos) return {};
		pos += key.size();
		if (pos >= tagBody.size()) return {};
		char q = tagBody[pos];
		if (q != '"' && q != '\'') return {};
		size_t close = tagBody.find(q, pos + 1);
		if (close == std::string::npos) return {};
		return tagBody.substr(pos + 1, close - (pos + 1));
	}

	bool parseFloats(const char* begin, const char* end, std::vector<float>& out)
	{
		out.clear();
		const char* p = begin;
		while (p < end)
		{
			while (p < end && std::isspace((unsigned char)*p)) ++p;
			if (p >= end) break;
			char* next = nullptr;
			float v = std::strtof(p, &next);
			if (next == p) return false;
			out.push_back(v);
			p = next;
		}
		return !out.empty();
	}

	std::string formatFloat(float v, int decimals)
	{
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
		return buf;
	}
}

bool XmlDoc::load(const std::string& path)
{
	rows.clear();
	groups.clear();
	raw.clear();
	loaded = false;
	dirty  = false;

	sourcePath = path;
	filename   = fs::path(path).filename().string();

	if (!readFileBytes(path, raw))
	{
		logger::write("warn", "XmlDoc: failed to read '%s'", path.c_str());
		return false;
	}

	if (raw.find("<timecycle_keyframe_data") != std::string::npos)
		kind = XmlKind::WeatherCycle;
	else if (raw.find("<timecycle_modifier_data") != std::string::npos)
		kind = XmlKind::ModifierList;
	else
		kind = XmlKind::Unknown;

	std::unordered_map<std::string, size_t> groupMap;
	std::vector<std::pair<std::string, std::string>> stack; // (tagName, groupLabel)

	size_t i = 0;
	const size_t n = raw.size();
	while (i < n)
	{
		size_t lt = raw.find('<', i);
		if (lt == std::string::npos) break;

		if (lt + 3 < n && raw.compare(lt, 4, "<!--") == 0)
		{
			size_t endC = raw.find("-->", lt + 4);
			i = (endC == std::string::npos) ? n : endC + 3;
			continue;
		}
		if (lt + 1 < n && (raw[lt + 1] == '?' || raw[lt + 1] == '!'))
		{
			size_t gt = raw.find('>', lt + 2);
			i = (gt == std::string::npos) ? n : gt + 1;
			continue;
		}

		size_t gt = raw.find('>', lt + 1);
		if (gt == std::string::npos) break;

		if (lt + 1 < n && raw[lt + 1] == '/')
		{
			// Closing tag
			if (!stack.empty()) stack.pop_back();
			i = gt + 1;
			continue;
		}

		bool selfClosing = (gt > lt + 1 && raw[gt - 1] == '/');
		std::string tagBody = raw.substr(lt + 1, (selfClosing ? gt - 1 : gt) - (lt + 1));

		size_t sp = 0;
		while (sp < tagBody.size() && !std::isspace((unsigned char)tagBody[sp])) ++sp;
		std::string tagName = tagBody.substr(0, sp);

		if (selfClosing)
		{
			i = gt + 1;
			continue;
		}

		// Check if leaf element (no '<' before matching '</tagName>')
		size_t contentStart = gt + 1;
		size_t nextLt = raw.find('<', contentStart);
		if (nextLt != std::string::npos && nextLt + 2 < n && raw[nextLt + 1] == '/')
		{
			size_t closeGt = raw.find('>', nextLt + 2);
			if (closeGt != std::string::npos)
			{
				std::string closeTag = raw.substr(nextLt + 2, closeGt - (nextLt + 2));
				if (closeTag == tagName)
				{
					std::vector<float> vals;
					if (parseFloats(raw.data() + contentStart, raw.data() + nextLt, vals))
					{
						std::string grp;
						for (auto it = stack.rbegin(); it != stack.rend(); ++it)
						{
							if (!it->second.empty())
							{
								grp = it->second;
								break;
							}
						}
						if (grp.empty()) grp = "DEFAULT";

						auto gIt = groupMap.find(grp);
						size_t gIdx;
						if (gIt == groupMap.end())
						{
							gIdx = groups.size();
							groupMap[grp] = gIdx;
							groups.push_back({ grp, {} });
						}
						else
						{
							gIdx = gIt->second;
						}

						size_t rIdx = rows.size();
						XmlRow r;
						r.group  = grp;
						r.name   = tagName;
						r.values = vals;
						r.orig   = std::move(vals);
						r.begin  = contentStart;
						r.end    = nextLt;
						r.dirty  = false;
						rows.push_back(std::move(r));
						groups[gIdx].rowIndices.push_back(rIdx);
					}
					i = closeGt + 1;
					continue;
				}
			}
		}

		// Container tag
		std::string label;
		if (tagName == "region" || tagName == "modifier")
			label = extractAttr(tagBody, "name");

		stack.emplace_back(std::move(tagName), std::move(label));
		i = gt + 1;
	}

	loaded = !rows.empty();
	return loaded;
}

std::string XmlDoc::renderModifiedText() const
{
	if (!dirty) return raw;

	std::string out;
	out.reserve(raw.size() + 4096);
	size_t cursor = 0;

	const int decimals = (kind == XmlKind::WeatherCycle) ? 4 : 3;

	for (const auto& r : rows)
	{
		if (!r.dirty) continue;
		if (r.begin < cursor || r.end > raw.size()) continue;

		out.append(raw, cursor, r.begin - cursor);
		if (kind == XmlKind::WeatherCycle)
		{
			for (size_t k = 0; k < r.values.size(); ++k)
			{
				out.push_back(' ');
				out.append(formatFloat(r.values[k], decimals));
			}
		}
		else
		{
			for (size_t k = 0; k < r.values.size(); ++k)
			{
				if (k > 0) out.push_back(' ');
				out.append(formatFloat(r.values[k], decimals));
			}
		}
		cursor = r.end;
	}

	if (cursor < raw.size())
		out.append(raw, cursor, raw.size() - cursor);
	return out;
}

bool XmlDoc::writeLiveCache(const std::string& liveDir) const
{
	std::string target = (fs::path(liveDir) / filename).string();
	return writeFileBytes(target, renderModifiedText());
}

bool XmlDoc::exportTo(const std::string& exportDir) const
{
	std::string target = (fs::path(exportDir) / filename).string();
	bool ok = writeFileBytes(target, renderModifiedText());
	if (ok) logger::write("info", "Exported '%s' -> '%s'", filename.c_str(), target.c_str());
	return ok;
}

bool XmlDoc::saveWithBackup(const std::string& backupDir)
{
	std::error_code ec;
	fs::create_directories(backupDir, ec);

	fs::path origBak = fs::path(backupDir) / (filename + ".orig");
	if (!fs::exists(origBak, ec) && fs::exists(sourcePath, ec))
		fs::copy_file(sourcePath, origBak, fs::copy_options::none, ec);

	// Rolling .bak1 .. .bak3
	fs::path b3 = fs::path(backupDir) / (filename + ".bak3");
	fs::path b2 = fs::path(backupDir) / (filename + ".bak2");
	fs::path b1 = fs::path(backupDir) / (filename + ".bak1");
	if (fs::exists(b2, ec)) fs::copy_file(b2, b3, fs::copy_options::overwrite_existing, ec);
	if (fs::exists(b1, ec)) fs::copy_file(b1, b2, fs::copy_options::overwrite_existing, ec);
	if (fs::exists(sourcePath, ec)) fs::copy_file(sourcePath, b1, fs::copy_options::overwrite_existing, ec);

	std::string text = renderModifiedText();
	if (!writeFileBytes(sourcePath, text))
		return false;

	logger::write("info", "Saved '%s' (with backup)", sourcePath.c_str());
	return load(sourcePath);
}

void XmlDoc::resetAll()
{
	for (auto& r : rows)
	{
		r.values = r.orig;
		r.dirty  = false;
	}
	dirty = false;
}

void XmlDoc::resetRow(size_t rowIdx)
{
	if (rowIdx >= rows.size()) return;
	rows[rowIdx].values = rows[rowIdx].orig;
	rows[rowIdx].dirty  = false;
	dirty = false;
	for (const auto& r : rows)
	{
		if (r.dirty) { dirty = true; break; }
	}
}

void XmlDoc::markRowDirty(size_t rowIdx)
{
	if (rowIdx >= rows.size()) return;
	rows[rowIdx].dirty = true;
	dirty = true;
}
