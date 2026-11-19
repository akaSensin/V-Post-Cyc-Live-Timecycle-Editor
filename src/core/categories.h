#pragma once
#include <array>
#include <cctype>
#include <cstring>
#include <string>

enum class VarCategory : int
{
	All = 0,
	Fog,
	MetalsAndReflections,
	LightingAndColors,
	SkyAndClouds,
	PostFX,
	WaterAndFX,
	ShadowsAndLOD,
	Count
};

inline const char* categoryName(VarCategory c)
{
	switch (c)
	{
	case VarCategory::All:                  return "All Variables";
	case VarCategory::Fog:                  return "Fog & Haze";
	case VarCategory::MetalsAndReflections: return "Metals & Reflections";
	case VarCategory::LightingAndColors:    return "Lighting & Colors";
	case VarCategory::SkyAndClouds:         return "Sky & Clouds";
	case VarCategory::PostFX:               return "PostFX & Tone Mapping";
	case VarCategory::WaterAndFX:           return "Water, Heat & Particles";
	case VarCategory::ShadowsAndLOD:        return "Shadows, LOD & Misc";
	default:                                return "Unknown";
	}
}

namespace tc_cat
{
	inline bool startsWith(const std::string& s, const char* prefix)
	{
		size_t n = std::strlen(prefix);
		return s.size() >= n && _strnicmp(s.c_str(), prefix, n) == 0;
	}

	inline bool equals(const std::string& s, const char* other)
	{
		return _stricmp(s.c_str(), other) == 0;
	}

	inline VarCategory classify(const std::string& name)
	{
		if (startsWith(name, "fog"))
			return VarCategory::Fog;

		if (startsWith(name, "reflection_") ||
			startsWith(name, "mirror_reflection_") ||
			startsWith(name, "water_reflection") ||
			equals(name, "water_specular_intensity") ||
			equals(name, "light_vehicle_second_spec_override") ||
			equals(name, "light_vehicle_intenity_scale") ||
			equals(name, "light_ped_rim_mult") ||
			equals(name, "light_amb_down_wrap"))
			return VarCategory::MetalsAndReflections;

		if (startsWith(name, "light_") ||
			startsWith(name, "ped_light_") ||
			startsWith(name, "ssao_") ||
			startsWith(name, "sun_direction_") ||
			startsWith(name, "moon_direction_") ||
			equals(name, "natural_ambient_multiplier") ||
			equals(name, "artificial_int_ambient_multiplier"))
			return VarCategory::LightingAndColors;

		if (startsWith(name, "sky_") || startsWith(name, "cloud_shadow_"))
			return VarCategory::SkyAndClouds;

		if (startsWith(name, "postfx_") ||
			startsWith(name, "dof_") ||
			startsWith(name, "bokeh_") ||
			startsWith(name, "environmental_blur_") ||
			startsWith(name, "lens_") ||
			startsWith(name, "chrom_aberration_") ||
			startsWith(name, "blur_vignetting_") ||
			startsWith(name, "screen_blur_") ||
			equals(name, "temperature"))
			return VarCategory::PostFX;

		if (startsWith(name, "water_") ||
			startsWith(name, "hh_") ||
			startsWith(name, "nv_") ||
			startsWith(name, "particle_") ||
			startsWith(name, "vfxlightning_") ||
			startsWith(name, "no_") ||
			startsWith(name, "wind_") ||
			equals(name, "vehicle_dirt_mod"))
			return VarCategory::WaterAndFX;

		return VarCategory::ShadowsAndLOD;
	}

	// Returns 0 for non-color, 1 for Red, 2 for Green, 3 for Blue, 4 for Alpha
	inline int colorChannel(const std::string& name)
	{
		if (name.size() < 2) return 0;
		auto endsWith = [&](const char* suf) {
			size_t n = std::strlen(suf);
			return name.size() >= n && _stricmp(name.c_str() + name.size() - n, suf) == 0;
		};
		if (endsWith("_col_r") || endsWith("_r")) return 1;
		if (endsWith("_col_g") || endsWith("_g")) return 2;
		if (endsWith("_col_b") || endsWith("_b")) return 3;
		if (endsWith("_col_a"))                   return 4;
		return 0;
	}

	inline constexpr std::array<const char*, 13> kKeyframeLabels = {
		"00:00 (Midnight)",
		"04:00 (Pre-Dawn)",
		"06:00 (Dawn)",
		"07:00 (Morning 1)",
		"08:00 (Morning 2)",
		"09:00 (Late AM)",
		"12:00 (Midday)",
		"14:00 (Afternoon)",
		"17:00 (Late PM)",
		"18:00 (Dusk)",
		"19:00 (Evening)",
		"20:00 (Night 1)",
		"22:00 (Night 2)"
	};

	inline constexpr std::array<int, 13> kKeyframeHours = {
		0, 4, 6, 7, 8, 9, 12, 14, 17, 18, 19, 20, 22
	};

	inline int hourToKeyframe(int hour)
	{
		int bestIdx = 0;
		int bestDist = 999;
		for (int i = 0; i < 13; ++i)
		{
			int d = hour - kKeyframeHours[i];
			if (d < 0) d = -d;
			if (d > 12) d = 24 - d;
			if (d < bestDist)
			{
				bestDist = d;
				bestIdx = i;
			}
		}
		return bestIdx;
	}
}
