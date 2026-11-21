#pragma once
#include "xml_doc.h"
#include <cstdint>
#include <string>
#include <vector>

namespace tc_engine
{
	struct ModPatch
	{
		std::string modifier;
		std::string var;
		float       v1 = 0.0f;
		float       v2 = 0.0f;
	};

	void installHooks();
	void loadDocuments();

	std::vector<XmlDoc>& cycleDocs();
	std::vector<XmlDoc>& modDocs();

	// Find cycle doc index by weather hash (e.g. from _GET_CURRENT_WEATHER_TYPE)
	int findCycleIndexByWeatherHash(uint32_t hash);
	const char* weatherNameForFilename(const std::string& filename);
	const char* weatherNameForHash(uint32_t hash);

	// Live apply helpers (marshal safely to sim thread)
	void applyCycleLive(size_t docIdx);
	void applyModifierLive(const std::string& modifier, const std::string& var, float v1, float v2);
	void requestFullCycleReload();

	// Save / Export / Discard
	int  exportAllModified();
	bool exportDoc(XmlDoc& doc);
	int  saveAllModified();
	bool saveDoc(XmlDoc& doc);
	void discardAllLive();

	bool isHookReady();
	bool isMarshalReady();
}
