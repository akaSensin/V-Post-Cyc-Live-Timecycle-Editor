#pragma once
#include <cstddef>
#include <string>
#include <vector>

struct XmlRow
{
	std::string        group;   // e.g. "GLOBAL" / "URBAN" (cycles) or modifier name (mods)
	std::string        name;    // variable tag name
	std::vector<float> values;  // 13 floats for weather cycles, 2 floats for modifiers
	std::vector<float> orig;    // original values loaded from disk
	size_t             begin = 0;
	size_t             end   = 0;
	bool               dirty = false;
};

struct XmlGroup
{
	std::string         name;
	std::vector<size_t> rowIndices;
};

enum class XmlKind
{
	Unknown = 0,
	WeatherCycle,
	ModifierList
};

struct XmlDoc
{
	std::string           filename;    // e.g. "w_clear.xml"
	std::string           sourcePath;  // path in timecycle/
	XmlKind               kind = XmlKind::Unknown;
	std::string           raw;
	std::vector<XmlRow>   rows;
	std::vector<XmlGroup> groups;
	bool                  loaded = false;
	bool                  dirty  = false;

	bool load(const std::string& path);
	std::string renderModifiedText() const;
	bool writeLiveCache(const std::string& liveDir) const;
	bool exportTo(const std::string& exportDir) const;
	bool saveWithBackup(const std::string& backupDir);
	void resetAll();
	void resetRow(size_t rowIdx);
	void markRowDirty(size_t rowIdx);
};
